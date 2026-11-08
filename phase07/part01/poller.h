#pragma once

#include <vector>
#include <unordered_map>


class Channel;

class Poller {
private:
    // fd -> Channel*，用于 update/remove 幂等
    std::unordered_map<int, Channel*> channels_;

public:
    using ChannelList = std::vector<Channel*>;

    Poller() = default;
};
