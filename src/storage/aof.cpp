#include "aof.h"

namespace redis {

bool AofLogger::Open() {
    // TODO: stage 6
    return true;
}

void AofLogger::Append(const RespCommand& /*cmd*/) {
    // TODO: stage 6
}

void AofLogger::Rewrite(std::function<void(const RespCommand&)> /*callback*/) {
    // TODO: stage 6
}

void AofLogger::Flush() {
    // TODO: stage 6
}

void AofLogger::Close() {
    // TODO: stage 6
}

}  // namespace redis
