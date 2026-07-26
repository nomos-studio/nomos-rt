// SPDX-License-Identifier: GPL-2.0-or-later
// nomos-mcp — MCP stdio bridge to the kairos IPC socket.
//
// Implements the Model Context Protocol (JSON-RPC 2.0 / Content-Length framing)
// and exposes one tool: evaluate.
//
//   evaluate(expression)
//     Prefix #v → :vcvrack-tty   (eval in Fennel, result pushed to VCVRack TTY)
//     Prefix #f → :fennel         (eval in Fennel, result returned to MCP)
//     Prefix #n → :nous           (forward to nous nREPL)
//     No prefix  → :fennel        (default)
//
// Usage: nomos-mcp [--socket <path>]
//   Default socket: /tmp/kairos.sock

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

// ---------------------------------------------------------------------------
// Minimal IPC helpers (mirrors nomos::rt::ipc wire format)
// ---------------------------------------------------------------------------

static constexpr uint8_t kMsgReplEval = 0x55;

static uint32_t bswap32(uint32_t v) noexcept {
    return ((v & 0xFF000000u) >> 24) | ((v & 0x00FF0000u) >> 8) | ((v & 0x0000FF00u) << 8) |
           ((v & 0x000000FFu) << 24);
}

static bool ipc_write(int fd, uint8_t type, std::string_view payload) {
    const uint32_t plen    = static_cast<uint32_t>(payload.size());
    const uint32_t plen_be = bswap32(plen);
    uint8_t        hdr[8]  = {};
    std::memcpy(hdr, &plen_be, 4);
    hdr[4] = type;
    if (write(fd, hdr, 8) != 8)
        return false;
    if (!payload.empty()) {
        const ssize_t written = write(fd, payload.data(), payload.size());
        if (written != static_cast<ssize_t>(payload.size()))
            return false;
    }
    return true;
}

static std::string ipc_read(int fd, uint8_t& out_type) {
    uint8_t hdr[8];
    if (read(fd, hdr, 8) != 8)
        return {};
    uint32_t plen_be;
    std::memcpy(&plen_be, hdr, 4);
    const uint32_t plen = bswap32(plen_be);
    out_type            = hdr[4];
    if (plen == 0)
        return {};
    std::string result(plen, '\0');
    ssize_t     got = 0;
    while (got < static_cast<ssize_t>(plen)) {
        const ssize_t n = read(fd, result.data() + got, plen - got);
        if (n <= 0)
            return {};
        got += n;
    }
    return result;
}

static int connect_kairos(const char* socket_path) {
    const int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0)
        return -1;
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path) - 1);
    if (connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

// ---------------------------------------------------------------------------
// Minimal JSON helpers
// ---------------------------------------------------------------------------

static std::string json_escape(std::string_view s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        if (c == '"')
            out += "\\\"";
        else if (c == '\\')
            out += "\\\\";
        else if (c == '\n')
            out += "\\n";
        else if (c == '\r')
            out += "\\r";
        else if (c == '\t')
            out += "\\t";
        else
            out += c;
    }
    return out;
}

// Extract the value of a JSON string field by key.  Simple state machine —
// handles escaped quotes correctly, does not handle nested objects.
static std::string json_str(std::string_view json, std::string_view key) {
    const std::string pat = "\"" + std::string(key) + "\"";
    auto              pos = json.find(pat);
    if (pos == std::string_view::npos)
        return {};
    pos += pat.size();
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == ':' || json[pos] == '\t'))
        ++pos;
    if (pos >= json.size() || json[pos] != '"')
        return {};
    ++pos;
    std::string result;
    bool        esc = false;
    while (pos < json.size()) {
        const char c = json[pos++];
        if (esc) {
            if (c == '"')
                result += '"';
            else if (c == '\\')
                result += '\\';
            else if (c == 'n')
                result += '\n';
            else if (c == 'r')
                result += '\r';
            else if (c == 't')
                result += '\t';
            else
                result += c;
            esc = false;
        } else if (c == '\\') {
            esc = true;
        } else if (c == '"') {
            break;
        } else {
            result += c;
        }
    }
    return result;
}

// Extract the raw JSON value for "id" (may be integer or string).
static std::string json_id_raw(std::string_view json) {
    const std::string_view kw  = "\"id\"";
    auto                   pos = json.find(kw);
    if (pos == std::string_view::npos)
        return "null";
    pos += kw.size();
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == ':'))
        ++pos;
    if (pos >= json.size())
        return "null";
    if (json[pos] == '"') {
        const std::string val = json_str(json, "id");
        return "\"" + json_escape(val) + "\"";
    }
    // Numeric or null
    auto end = json.find_first_of(",}", pos);
    if (end == std::string_view::npos)
        end = json.size();
    while (end > pos && json[end - 1] == ' ')
        --end;
    return std::string(json.substr(pos, end - pos));
}

// ---------------------------------------------------------------------------
// MCP I/O — Content-Length framing
// ---------------------------------------------------------------------------

static std::string mcp_read() {
    // Read headers until blank line.
    int         content_length = -1;
    std::string header_buf;
    while (true) {
        int c = std::fgetc(stdin);
        if (c == EOF)
            return {};
        header_buf += static_cast<char>(c);
        if (header_buf.size() >= 4 && header_buf.substr(header_buf.size() - 4) == "\r\n\r\n") {
            // Parse Content-Length
            const auto pos = header_buf.find("Content-Length:");
            if (pos != std::string::npos)
                content_length = std::atoi(header_buf.c_str() + pos + 15);
            break;
        }
        // Also handle bare \n\n (some clients omit CR)
        if (header_buf.size() >= 2 && header_buf.substr(header_buf.size() - 2) == "\n\n") {
            const auto pos = header_buf.find("Content-Length:");
            if (pos != std::string::npos)
                content_length = std::atoi(header_buf.c_str() + pos + 15);
            break;
        }
    }
    if (content_length <= 0)
        return {};
    std::string body(static_cast<std::size_t>(content_length), '\0');
    if (std::fread(body.data(), 1, static_cast<std::size_t>(content_length), stdin) !=
        static_cast<std::size_t>(content_length))
        return {};
    return body;
}

static void mcp_write(const std::string& body) {
    const std::string hdr = "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n";
    std::fwrite(hdr.data(), 1, hdr.size(), stdout);
    std::fwrite(body.data(), 1, body.size(), stdout);
    std::fflush(stdout);
}

static void mcp_error(std::string_view id_raw, int code, std::string_view msg) {
    const std::string body = "{\"jsonrpc\":\"2.0\",\"id\":" + std::string(id_raw) +
                             ",\"error\":{\"code\":" + std::to_string(code) + ",\"message\":\"" +
                             json_escape(msg) + "\"}}";
    mcp_write(body);
}

static void mcp_result(std::string_view id_raw, std::string_view result_json) {
    const std::string body = "{\"jsonrpc\":\"2.0\",\"id\":" + std::string(id_raw) +
                             ",\"result\":" + std::string(result_json) + "}";
    mcp_write(body);
}

// ---------------------------------------------------------------------------
// Tool: evaluate
// ---------------------------------------------------------------------------

static void handle_evaluate(std::string_view id_raw, std::string_view expression,
                            const char* socket_path) {
    std::string_view expr = expression;

    // Determine destination from prefix.
    std::string_view dest = "fennel";
    if (expr.size() >= 3 && expr[0] == '#') {
        if (expr[1] == 'v') {
            dest = "vcvrack-tty";
            expr = expr.substr(2);
        } else if (expr[1] == 'f') {
            dest = "fennel";
            expr = expr.substr(2);
        } else if (expr[1] == 'n') {
            dest = "nous";
            expr = expr.substr(2);
        }
    }
    // Trim leading whitespace after prefix.
    while (!expr.empty() && (expr[0] == ' ' || expr[0] == '\t'))
        expr = expr.substr(1);

    const int fd = connect_kairos(socket_path);
    if (fd < 0) {
        mcp_error(id_raw, -32603, "cannot connect to kairos socket");
        return;
    }

    // Build msg_repl_eval EDN payload.
    const std::string payload = "{:dest :" + std::string(dest) + " :payload \"" +
                                json_escape(expr) + "\"" + " :id \"mcp\"}";

    if (!ipc_write(fd, kMsgReplEval, payload)) {
        close(fd);
        mcp_error(id_raw, -32603, "failed to send eval to kairos");
        return;
    }

    // Read response — kairos replies on same connection with msg_repl_eval.
    uint8_t     rtype = 0;
    std::string resp  = ipc_read(fd, rtype);
    close(fd);

    if (resp.empty()) {
        mcp_error(id_raw, -32603, "no response from kairos");
        return;
    }

    // resp is EDN: {:id "mcp" :result <value>}
    // Return as MCP tool result content.
    const std::string result_json =
        "{\"content\":[{\"type\":\"text\",\"text\":\"" + json_escape(resp) + "\"}]}";
    mcp_result(id_raw, result_json);
}

// ---------------------------------------------------------------------------
// MCP capability responses
// ---------------------------------------------------------------------------

static const char* k_tool_schema = R"json({
  "name": "evaluate",
  "description": "Evaluate an expression in the nomos-rt runtime via kairos IPC. Prefix: #v → VCVRack TTY, #f → Fennel (default), #n → nous.",
  "inputSchema": {
    "type": "object",
    "properties": {
      "expression": {
        "type": "string",
        "description": "Expression to evaluate. Optional prefix: #v (VCVRack TTY), #f (Fennel), #n (nous)."
      }
    },
    "required": ["expression"]
  }
})json";

static void handle_initialize(std::string_view id_raw) {
    mcp_result(id_raw, "{\"protocolVersion\":\"2024-11-05\","
                       "\"capabilities\":{\"tools\":{}},"
                       "\"serverInfo\":{\"name\":\"nomos-mcp\",\"version\":\"0.1.0\"}}");
}

static void handle_tools_list(std::string_view id_raw) {
    std::string body = "{\"tools\":[";
    body += k_tool_schema;
    body += "]}";
    mcp_result(id_raw, body);
}

// ---------------------------------------------------------------------------
// Main loop
// ---------------------------------------------------------------------------

int main(int argc, char* argv[]) {
    const char* socket_path = "/tmp/kairos.sock";

    for (int i = 1; i < argc - 1; ++i) {
        if (std::strcmp(argv[i], "--socket") == 0)
            socket_path = argv[i + 1];
    }

    while (true) {
        const std::string body = mcp_read();
        if (body.empty())
            break;

        const std::string id_raw = json_id_raw(body);
        const std::string method = json_str(body, "method");

        if (method == "initialize") {
            handle_initialize(id_raw);
        } else if (method == "notifications/initialized") {
            // No response needed for notifications.
        } else if (method == "tools/list") {
            handle_tools_list(id_raw);
        } else if (method == "tools/call") {
            const std::string name = json_str(body, "name");
            if (name == "evaluate") {
                const std::string expr = json_str(body, "expression");
                if (expr.empty()) {
                    mcp_error(id_raw, -32602, "missing required argument: expression");
                } else {
                    handle_evaluate(id_raw, expr, socket_path);
                }
            } else {
                mcp_error(id_raw, -32601, "unknown tool");
            }
        } else {
            // Notifications or unknown methods — no response.
            if (id_raw != "null")
                mcp_error(id_raw, -32601, "method not found");
        }
    }

    return 0;
}
