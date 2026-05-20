#include "router.h"
#include <algorithm>
#include <cctype>

namespace redis {

CommandRouter::CommandRouter() {}

void CommandRouter::Register(const std::string& name, CmdHandler handler) {
    table_[Normalize(name)] = std::move(handler);
}

std::string CommandRouter::Execute(CmdContext& ctx) const {
    auto it = table_.find(Normalize(ctx.cmd.name));
    if (it == table_.end()) {
        return RespCodec::Error("ERR unknown command '" + ctx.cmd.name + "'");
    }
    return it->second(ctx);
}

std::string CommandRouter::Normalize(std::string_view name) {
    std::string out;
    out.reserve(name.size());
    for (char c : name) {
        out += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return out;
}

}  // namespace redis
