#include "plugin/chunklet_plugin.h"

#include "native/chunk_source.h"
#include "render/plan.h"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <exception>
#include <string_view>

namespace chunklet {
namespace {

bool parse_integer(std::string_view text, int &value)
{
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    return error == std::errc{} && end == text.data() + text.size();
}

std::string lower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

}  // namespace

bool ChunkletPlugin::onCommand(endstone::CommandSender &sender,
                               const endstone::Command &command,
                               const std::vector<std::string> &args)
{
    if (command.getName() != "chunklet") {
        return false;
    }
    if (sender.asConsole() == nullptr) {
        sender.sendErrorMessage("Chunklet commands are available only in the server console.");
        return true;
    }

    const auto action = args.empty() ? std::string("status") : lower(args.front());
    if (action == "start") {
        handleStart(sender, args);
    } else if (action == "stop") {
        handleStop(sender);
    } else if (action == "status") {
        handleStatus(sender);
    } else {
        sender.sendErrorMessage("Usage: chunklet start|status|stop");
    }
    return true;
}

std::optional<ChunkletPlugin::StartArguments> ChunkletPlugin::parseStart(
    endstone::CommandSender &sender, const std::vector<std::string> &args) const
{
    if (args.size() != 3 && args.size() != 5) {
        sender.sendErrorMessage(
            "Usage: chunklet start <radius> <dimension> [center-x center-z]");
        return std::nullopt;
    }

    int radius = 0;
    int center_x = 0;
    int center_z = 0;
    if (!parse_integer(args[1], radius) ||
        (args.size() == 5 &&
         (!parse_integer(args[3], center_x) || !parse_integer(args[4], center_z)))) {
        sender.sendErrorMessage("Radius and center coordinates must be integers.");
        return std::nullopt;
    }
    if (radius < 1) {
        sender.sendErrorMessage("Radius must be at least 1 block.");
        return std::nullopt;
    }

    const auto dimension = lower(args[2]);
    if (dimension == "overworld") {
        return StartArguments{radius, endstone::Dimension::Type::Overworld,
                              dimension, center_x, center_z};
    }
    if (dimension == "nether") {
        return StartArguments{radius, endstone::Dimension::Type::Nether,
                              dimension, center_x, center_z};
    }
    if (dimension == "the_end" || dimension == "end") {
        return StartArguments{radius, endstone::Dimension::Type::TheEnd,
                              "the_end", center_x, center_z};
    }
    sender.sendErrorMessage("Dimension must be overworld, nether, or the_end.");
    return std::nullopt;
}

endstone::Dimension *ChunkletPlugin::findDimension(endstone::Dimension::Type type) const
{
    auto *level = getServer().getLevel();
    if (level == nullptr) {
        return nullptr;
    }
    for (auto *dimension : level->getDimensions()) {
        if (dimension != nullptr && dimension->getType() == type) {
            return dimension;
        }
    }
    return nullptr;
}

void ChunkletPlugin::handleStart(endstone::CommandSender &sender,
                                 const std::vector<std::string> &args)
{
    if (job_) {
        sender.sendErrorMessage("A render is active. Run 'chunklet stop' first.");
        return;
    }
    const auto parsed = parseStart(sender, args);
    if (!parsed) {
        return;
    }
    auto *dimension = findDimension(parsed->dimension);
    if (dimension == nullptr) {
        sender.sendErrorMessage("Dimension '{}' is not available.", parsed->dimension_name);
        return;
    }

    try {
        const auto bounds = render::square_bounds(parsed->center_x, parsed->center_z,
                                                   parsed->radius);
        constexpr int generation_halo = 8;
        auto positions = render::center_out(bounds.expanded(generation_halo));
        const auto window = positions.size();
        auto source = native::ChunkSource::resolve(dimension);
        job_.emplace(dimension, source, bounds, std::move(positions), window);
        sender.sendMessage(
            "Chunklet started: {} chunks, dimension={}, center={},{}.",
            job_->snapshot().total, parsed->dimension_name,
            parsed->center_x, parsed->center_z);

        std::string error;
        if (!job_->start(error)) {
            failActive(error);
        } else if (job_->finished()) {
            finishActive();
        }
    } catch (const std::exception &exception) {
        sender.sendErrorMessage("Chunklet could not start: {}", exception.what());
        job_.reset();
    }
}

void ChunkletPlugin::handleStop(endstone::CommandSender &sender)
{
    if (!job_) {
        sender.sendMessage("Chunklet is idle.");
        return;
    }
    job_->cancel();
    last_ = job_->snapshot();
    job_.reset();
    sender.sendMessage("Chunklet stopped. {}", format(*last_));
}

void ChunkletPlugin::handleStatus(endstone::CommandSender &sender) const
{
    if (job_) {
        sender.sendMessage("Chunklet active. {}", format(job_->snapshot()));
    } else if (last_) {
        sender.sendMessage("Chunklet idle. Last run: {}", format(*last_));
    } else {
        sender.sendMessage("Chunklet is idle. No run has started.");
    }
}

}  // namespace chunklet
