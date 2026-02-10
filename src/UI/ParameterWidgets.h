#pragma once

#include "../Application/ParameterRegistry.h"
#include <functional>

class UI;
class SceneObject;
struct ParameterMetadata;

namespace ParameterWidgets {

enum class WidgetStyle {
    Compact,
    Standard,
    Detailed
};

void RenderParameter(const ParameterHandle& handle, UI* ui, WidgetStyle style = WidgetStyle::Standard);

void RenderParameterGroup(ParameterGroup group, UI* ui, WidgetStyle style = WidgetStyle::Standard,
                          bool defaultOpen = true);

void RenderParameterGroupWithFilter(ParameterGroup group, UI* ui,
                                    std::function<bool(const ParameterMetadata&)> filter,
                                    WidgetStyle style = WidgetStyle::Standard,
                                    bool defaultOpen = true);

bool RenderObjectParameter(SceneObject* object, const ParameterHandle& handle, const ParameterMetadata& meta,
                           WidgetStyle style = WidgetStyle::Standard);

void RenderSceneObjectParameters(SceneObject* object, WidgetStyle style = WidgetStyle::Standard);

const char* GetGroupDisplayName(ParameterGroup group);

}

