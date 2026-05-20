#include "rdb.h"

namespace redis {

bool RdbSaver::Save(const Dict& /*db*/) {
    // TODO: stage 6
    return true;
}

bool RdbSaver::Load(Dict& /*db*/) {
    // TODO: stage 6
    return true;
}

void RdbSaver::WriteType(std::ofstream& /*os*/, ObjType /*type*/) {}
void RdbSaver::WriteLength(std::ofstream& /*os*/, size_t /*len*/) {}
void RdbSaver::WriteString(std::ofstream& /*os*/, const std::string& /*s*/) {}
void RdbSaver::WriteDouble(std::ofstream& /*os*/, double /*val*/) {}

}  // namespace redis
