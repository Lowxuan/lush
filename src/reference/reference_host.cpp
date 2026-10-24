#include "reference/reference_host.h"

#include "lush/core/format.h"

#include <cassert>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

namespace lush::reference {
namespace {

using lush::command::CommandResult;
using lush::command::CommandSpec;
using lush::command::ParamSpec;
using lush::command::ParamType;
using lush::command::RegistrationResult;
using lush::core::CommandContext;
using lush::core::Output;
using lush::core::Value;

ParamSpec pointParam(std::string name, std::string hint) {
    ParamSpec param;
    param.name = std::move(name);
    param.type = ParamType::Point;
    param.dim = 2;
    param.hint = std::move(hint);
    return param;
}

ParamSpec numberParam(std::string name) {
    ParamSpec param;
    param.name = std::move(name);
    param.type = ParamType::Number;
    return param;
}

CommandResult lineHandler(const std::vector<Value>& args,
                          const CommandContext&, Output& output) {
    output.write("LINE " + lush::core::formatValue(args[0]) + " " +
                 lush::core::formatValue(args[1]) + "\n");
    return CommandResult::done();
}

CommandResult circleHandler(const std::vector<Value>& args,
                            const CommandContext&, Output& output) {
    output.write("CIRCLE " + lush::core::formatValue(args[0]) + " " +
                 lush::core::formatValue(args[1]) + "\n");
    return CommandResult::done();
}

CommandResult moveHandler(const std::vector<Value>& args,
                          const CommandContext&, Output& output) {
    output.write("MOVE " + lush::core::formatValue(args[0]) + " " +
                 lush::core::formatValue(args[1]) + "\n");
    return CommandResult::done();
}

void requireRegistered(RegistrationResult result) {
    if (!result.ok()) {
        assert(false && "reference host registration failed");
        std::abort();
    }
}

} // namespace

ReferenceHost::ReferenceHost() {
    CommandSpec line;
    line.name = "line";
    line.params = {pointParam("p1", "指定第一个点"),
                   pointParam("p2", "指定第二个点")};
    line.interactive = true;
    line.help = "绘制线段";
    requireRegistered(registry_.registerCommand(std::move(line), lineHandler));

    CommandSpec circle;
    circle.name = "circle";
    circle.params = {pointParam("c", "指定圆心"), numberParam("r")};
    circle.params[1].hint = "指定半径";
    circle.interactive = true;
    circle.help = "绘制圆";
    requireRegistered(
        registry_.registerCommand(std::move(circle), circleHandler));

    CommandSpec move;
    move.name = "move";
    move.params = {numberParam("dx"), numberParam("dy")};
    move.help = "移动";
    requireRegistered(registry_.registerCommand(std::move(move), moveHandler));
}

} // namespace lush::reference
