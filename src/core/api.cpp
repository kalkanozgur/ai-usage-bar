#include "core/api.hpp"

#include "core/json_lite.hpp"
#include "core/time_util.hpp"

namespace cub::api {
namespace {

std::optional<UsageWindow> read_window(std::string_view doc, json::Span root,
                                       std::string_view key) {
    const auto v = json::member(doc, root, key);
    if (!v || !json::is_object(doc, *v)) {
        return std::nullopt;  // absent or null: window does not apply to this plan
    }
    UsageWindow w;
    if (const auto u = json::member(doc, *v, "utilization")) {
        w.utilization = json::as_number(doc, *u).value_or(0.0);
    }
    if (const auto r = json::member(doc, *v, "resets_at")) {
        if (const auto s = json::as_string(doc, *r)) {
            w.resets_at = time::parse_iso8601(*s);
        }
    }
    return w;
}

}  // namespace

std::optional<UsageSnapshot> parse_usage(std::string_view doc) {
    const auto root = json::root(doc);
    if (!root || !json::is_object(doc, *root)) {
        return std::nullopt;
    }
    if (!json::member(doc, *root, "five_hour") && !json::member(doc, *root, "seven_day")) {
        return std::nullopt;  // e.g. an error payload
    }
    UsageSnapshot snap;
    snap.five_hour = read_window(doc, *root, "five_hour");
    snap.seven_day = read_window(doc, *root, "seven_day");
    snap.seven_day_sonnet = read_window(doc, *root, "seven_day_sonnet");
    snap.seven_day_opus = read_window(doc, *root, "seven_day_opus");
    return snap;
}

std::optional<std::string> pick_chat_org(std::string_view doc) {
    const auto root = json::root(doc);
    if (!root || !json::is_array(doc, *root)) {
        return std::nullopt;
    }
    std::optional<std::string> first;
    std::optional<std::string> chat;
    json::for_each_element(doc, *root, [&](json::Span org) {
        const auto uuid_span = json::member(doc, org, "uuid");
        if (!uuid_span) {
            return true;
        }
        auto uuid = json::as_string(doc, *uuid_span);
        if (!uuid || uuid->empty()) {
            return true;
        }
        if (!first) {
            first = *uuid;
        }
        if (const auto caps = json::member(doc, org, "capabilities")) {
            bool has_chat = false;
            json::for_each_element(doc, *caps, [&](json::Span cap) {
                has_chat = json::as_string(doc, cap) == "chat";
                return !has_chat;
            });
            if (has_chat) {
                chat = std::move(uuid);
                return false;
            }
        }
        return true;
    });
    return chat ? chat : first;
}

}  // namespace cub::api
