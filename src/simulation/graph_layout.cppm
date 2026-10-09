export module MoleHole:Simulation.GraphLayout;

import std;
import glm;
import :Simulation.AnimationGraph;

export namespace MoleHole
{
    struct NodeRect
    {
        int Id{0};
        glm::vec2 Position{0.0f};
        glm::vec2 Size{0.0f};
    };

    enum class AlignMode { Left, Right, Top, Bottom, CenterHorizontal, CenterVertical };

    [[nodiscard]] inline std::vector<NodeRect> AlignRects(std::vector<NodeRect> rects, const AlignMode mode)
    {
        if (rects.size() < 2) return rects;
        float minX = std::numeric_limits<float>::max(), maxX = std::numeric_limits<float>::lowest();
        float minY = minX, maxY = maxX;
        for (const auto& r : rects)
        {
            minX = std::min(minX, r.Position.x);
            maxX = std::max(maxX, r.Position.x + r.Size.x);
            minY = std::min(minY, r.Position.y);
            maxY = std::max(maxY, r.Position.y + r.Size.y);
        }
        for (auto& r : rects)
        {
            switch (mode)
            {
            case AlignMode::Left: r.Position.x = minX; break;
            case AlignMode::Right: r.Position.x = maxX - r.Size.x; break;
            case AlignMode::Top: r.Position.y = minY; break;
            case AlignMode::Bottom: r.Position.y = maxY - r.Size.y; break;
            case AlignMode::CenterHorizontal: r.Position.x = (minX + maxX) * 0.5f - r.Size.x * 0.5f; break;
            case AlignMode::CenterVertical: r.Position.y = (minY + maxY) * 0.5f - r.Size.y * 0.5f; break;
            }
        }
        return rects;
    }

    // Keeps the outermost rects in place and equalises the gaps between all of them along one axis.
    [[nodiscard]] inline std::vector<NodeRect> DistributeRects(std::vector<NodeRect> rects, const bool horizontal)
    {
        if (rects.size() < 3) return rects;
        const int axis = horizontal ? 0 : 1;
        std::ranges::sort(rects, [axis](const NodeRect& a, const NodeRect& b) { return a.Position[axis] < b.Position[axis]; });

        const float start = rects.front().Position[axis];
        const float end = rects.back().Position[axis] + rects.back().Size[axis];
        float occupied = 0.0f;
        for (const auto& r : rects) occupied += r.Size[axis];
        const float gap = (end - start - occupied) / static_cast<float>(rects.size() - 1);

        float cursor = start;
        for (auto& r : rects)
        {
            r.Position[axis] = cursor;
            cursor += r.Size[axis] + gap;
        }
        return rects;
    }

    [[nodiscard]] inline std::pair<glm::vec2, glm::vec2> BoundsOf(const std::vector<NodeRect>& rects, const float padding,
                                                                  const float titleHeight = 0.0f)
    {
        if (rects.empty()) return {glm::vec2(0.0f), glm::vec2(0.0f)};
        glm::vec2 lo(std::numeric_limits<float>::max());
        glm::vec2 hi(std::numeric_limits<float>::lowest());
        for (const auto& r : rects)
        {
            lo = glm::min(lo, r.Position);
            hi = glm::max(hi, r.Position + r.Size);
        }
        lo -= glm::vec2(padding, padding + titleHeight);
        hi += glm::vec2(padding);
        return {lo, hi - lo};
    }

    [[nodiscard]] inline bool CommentContains(const Comment& comment, const NodeRect& rect)
    {
        const glm::vec2 center = rect.Position + rect.Size * 0.5f;
        return center.x >= comment.Position.x && center.x <= comment.Position.x + comment.Size.x &&
               center.y >= comment.Position.y && center.y <= comment.Position.y + comment.Size.y;
    }

    [[nodiscard]] inline std::vector<int> NodesInsideComment(const Comment& comment, const std::vector<NodeRect>& rects)
    {
        std::vector<int> ids;
        for (const auto& r : rects) if (CommentContains(comment, r)) ids.push_back(r.Id);
        return ids;
    }
}
