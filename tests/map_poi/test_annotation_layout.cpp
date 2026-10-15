#include "ui_map_runtime/map_poi/annotation_layout.h"
#include <cassert>
#include <cstring>
#include <vector>

using namespace ui::map_poi;
using ui::map::AnnotationKind;

bool measure(void*, const char* text, std::size_t bytes, bool ellipsis, int16_t& width, int16_t& height)
{
    assert((static_cast<uint8_t>(text[bytes]) & 0xC0U) != 0x80U);
    unsigned characters = 0;
    for (std::size_t i = 0; i < bytes; ++i)
        if ((static_cast<uint8_t>(text[i]) & 0xC0U) != 0x80U) ++characters;
    width = static_cast<int16_t>(characters * 7 + (ellipsis ? 7 : 0));
    height = 14;
    return true;
}

AnnotationCandidate point(uint64_t id, int x, int y, const char* name, uint8_t priority = 100)
{
    AnnotationCandidate c;
    c.key = c.feature_key = id;
    c.x = static_cast<int16_t>(x);
    c.y = static_cast<int16_t>(y);
    c.name = name;
    c.category = "water";
    c.priority = priority;
    return c;
}

void named_points_own_their_marker_space()
{
    std::vector<AnnotationCandidate> candidates{point(1, 10, 10, "Water"), point(2, 40, 10, "")};
    std::vector<AnnotationPlacement> placed(48);
    AnnotationLayoutOptions options;
    options.width = 100;
    options.height = 20;
    auto result = layout_annotations(candidates.data(), candidates.size(), nullptr, 0, placed.data(), placed.size(), options, measure, nullptr);
    assert(result.labels == 1 && result.unnamed_markers == 0);
    assert(placed[0].key == 1 && placed[0].marker && placed[0].text_bytes == 5);
}

void rejected_front_candidates_do_not_starve_later_labels()
{
    std::vector<AnnotationCandidate> candidates;
    for (int i = 0; i < 60; ++i) candidates.push_back(point(i + 1, 20, 20, "Dense", 200));
    for (int i = 0; i < 12; ++i) candidates.push_back(point(i + 100, 110 + i % 4 * 90, 30 + i / 4 * 60, "Named", 100));
    std::vector<AnnotationPlacement> placed(48);
    AnnotationLayoutOptions options;
    options.width = 480;
    options.height = 222;
    auto result = layout_annotations(candidates.data(), candidates.size(), nullptr, 0, placed.data(), placed.size(), options, measure, nullptr);
    assert(result.labels > 8 && result.collisions >= 59);
}

void stable_labels_survive_minor_distance_changes()
{
    std::vector<AnnotationCandidate> candidates{point(1, 20, 25, "Old"), point(2, 70, 25, "New")};
    std::vector<AnnotationPlacement> placed(48);
    placed[0].key = 1;
    AnnotationLayoutOptions options;
    options.width = 150;
    options.height = 60;
    options.max_labels = 1;
    auto result = layout_annotations(candidates.data(), candidates.size(), placed.data(), 1, placed.data(), placed.size(), options, measure, nullptr);
    assert(result.labels == 1 && placed[0].key == 1);
    candidates[1].priority = 140;
    result = layout_annotations(candidates.data(), candidates.size(), placed.data(), 1, placed.data(), placed.size(), options, measure, nullptr);
    assert(result.labels == 1 && placed[0].key == 2);
}

void roads_use_visible_segments_and_do_not_need_markers()
{
    auto road = point(1, -900, 40, "Road");
    road.kind = AnnotationKind::Road;
    road.path_points = 2;
    road.path[0] = -32;
    road.path[1] = 40;
    road.path[2] = 200;
    road.path[3] = 40;
    std::vector<AnnotationCandidate> candidates{road};
    std::vector<AnnotationPlacement> placed(48);
    AnnotationLayoutOptions options;
    options.width = 100;
    options.height = 80;
    auto result = layout_annotations(candidates.data(), candidates.size(), nullptr, 0, placed.data(), placed.size(), options, measure, nullptr);
    assert(result.road_labels == 1 && !placed[0].marker && placed[0].x >= 0 && placed[0].x < 100);
}

void long_utf8_names_are_shortened_instead_of_removed()
{
    std::vector<AnnotationCandidate> candidates{point(1, 10, 30, u8"昆明主城区非常长的道路与设施名称")};
    std::vector<AnnotationPlacement> placed(48);
    AnnotationLayoutOptions options;
    options.width = 120;
    options.height = 60;
    options.max_text_width = 56;
    auto result = layout_annotations(candidates.data(), candidates.size(), nullptr, 0, placed.data(), placed.size(), options, measure, nullptr);
    assert(result.labels == 1 && result.truncated == 1 && placed[0].ellipsis);
    assert(placed[0].text_bytes > 0 && placed[0].text_bytes % 3 == 0);
}

void reservations_prevent_facilities_from_removing_all_roads()
{
    std::vector<AnnotationCandidate> candidates;
    for (int i = 0; i < 10; ++i) candidates.push_back(point(i + 1, 20 + i % 5 * 90, 30 + i / 5 * 80, "POI", 255));
    auto road = point(100, 200, 110, "Street", 100);
    road.kind = AnnotationKind::Road;
    road.path_points = 2;
    road.path[0] = 10;
    road.path[1] = 110;
    road.path[2] = 470;
    road.path[3] = 110;
    candidates.push_back(road);
    std::vector<AnnotationPlacement> placed(48);
    AnnotationLayoutOptions options;
    options.width = 480;
    options.height = 222;
    options.max_labels = 4;
    auto result = layout_annotations(candidates.data(), candidates.size(), nullptr, 0, placed.data(), placed.size(), options, measure, nullptr);
    assert(result.road_labels == 1 && result.labels == 4);
}

void unnamed_clusters_only_join_the_same_category()
{
    std::vector<AnnotationCandidate> candidates{point(1, 20, 20, ""), point(2, 24, 20, ""), point(3, 60, 20, "")};
    candidates[2].category = "parking";
    std::vector<AnnotationPlacement> placed(48);
    AnnotationLayoutOptions options;
    options.width = 120;
    options.height = 60;
    auto result = layout_annotations(candidates.data(), candidates.size(), nullptr, 0, placed.data(), placed.size(), options, measure, nullptr);
    assert(result.unnamed_markers == 2 && placed[0].cluster_count == 2 && placed[1].cluster_count == 1);
}

void denser_zoom_policy_uses_existing_storage_without_overlapping_labels()
{
    std::vector<AnnotationCandidate> candidates;
    for (int row = 0; row < 7; ++row)
        for (int column = 0; column < 8; ++column)
            candidates.push_back(point(1 + row * 8 + column, 18 + column * 38, 16 + row * 27, "P"));
    std::vector<AnnotationPlacement> placed(AnnotationLayoutOptions::kMaxPlacements);
    AnnotationLayoutOptions options;
    options.width = 320;
    options.height = 210;
    for (const int zoom : {6, 11, 15, 16})
    {
        const unsigned old_budget = zoom <= 6 ? 8 : zoom <= 11 ? 16
                                                : zoom <= 15   ? 24
                                                               : 32;
        options.max_labels = static_cast<uint8_t>(old_budget);
        const auto before = layout_annotations(candidates.data(), candidates.size(), nullptr, 0, placed.data(), placed.size(), options, measure, nullptr);
        options.max_labels = annotation_label_budget(zoom);
        const auto after = layout_annotations(candidates.data(), candidates.size(), nullptr, 0, placed.data(), placed.size(), options, measure, nullptr);
        assert(after.poi_labels * 100 >= before.poi_labels * 135 && after.poi_labels * 100 <= before.poi_labels * 145);
        assert(after.count <= placed.size());
        for (size_t first = 0; first < after.count; ++first)
            for (size_t second = first + 1; second < after.count; ++second)
            {
                const auto& a = placed[first];
                const auto& b = placed[second];
                assert(a.text_x + a.text_width + options.padding <= b.text_x || b.text_x + b.text_width + options.padding <= a.text_x ||
                       a.text_y + a.text_height + options.padding <= b.text_y || b.text_y + b.text_height + options.padding <= a.text_y);
            }
    }
}

int main()
{
    named_points_own_their_marker_space();
    rejected_front_candidates_do_not_starve_later_labels();
    stable_labels_survive_minor_distance_changes();
    roads_use_visible_segments_and_do_not_need_markers();
    long_utf8_names_are_shortened_instead_of_removed();
    reservations_prevent_facilities_from_removing_all_roads();
    unnamed_clusters_only_join_the_same_category();
    denser_zoom_policy_uses_existing_storage_without_overlapping_labels();
}
