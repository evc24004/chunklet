#pragma once

#include <optional>
#include <string>
#include <vector>

#include <endstone/endstone.hpp>

#include "render/job.h"

namespace chunklet {

class ChunkletPlugin : public endstone::Plugin {
public:
    void onLoad() override;
    void onEnable() override;
    void onDisable() override;
    bool onCommand(endstone::CommandSender &sender, const endstone::Command &command,
                   const std::vector<std::string> &args) override;
    void onChunkLoad(const endstone::ChunkLoadEvent &event);

private:
    struct StartArguments {
        int radius;
        endstone::Dimension::Type dimension;
        std::string dimension_name;
        int center_x;
        int center_z;
    };

    void handleStart(endstone::CommandSender &sender,
                     const std::vector<std::string> &args);
    void handleStop(endstone::CommandSender &sender);
    void handleStatus(endstone::CommandSender &sender) const;
    [[nodiscard]] std::optional<StartArguments> parseStart(
        endstone::CommandSender &sender, const std::vector<std::string> &args) const;
    [[nodiscard]] endstone::Dimension *findDimension(endstone::Dimension::Type type) const;
    void persistActive();
    void finishActive();
    void failActive(const std::string &error);
    [[nodiscard]] static std::string format(const render::JobSnapshot &snapshot);

    std::optional<render::RenderJob> job_;
    std::optional<render::JobSnapshot> last_;
};

}  // namespace chunklet
