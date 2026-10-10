#include <imgui.h>

import GPP;
import MoleHole;
import std;

#include <gpp/hot_reload_export.h>

using namespace GPP;
using namespace MoleHole;

namespace
{
    std::string BuildMetricLatex(const int metricType)
    {
        std::array<std::string, 16> m;
        m.fill("0");
        switch (metricType)
        {
        case 0: // Schwarzschild
            m[0] = "-(1 - \\frac{r_s}{r})";
            m[5] = "\\frac{1}{1 - \\frac{r_s}{r}}";
            m[10] = "r^2";
            m[15] = "r^2 \\sin^2(\\theta)";
            break;
        case 1: // Kerr
            m[0] = "-(1 - \\frac{r_s r}{\\rho^2})";
            m[3] = m[12] = "-\\frac{r_s r a \\sin^2(\\theta)}{\\rho^2}";
            m[5] = "\\frac{\\rho^2}{\\Delta}";
            m[10] = "\\rho^2";
            m[15] = "\\left(r^2 + a^2 + \\frac{r_s r a^2 \\sin^2(\\theta)}{\\rho^2}\\right)\\sin^2(\\theta)";
            break;
        case 2: // Reissner-Nordstrom
            m[0] = "-f(r)";
            m[5] = "\\frac{1}{f(r)}";
            m[10] = "r^2";
            m[15] = "r^2 \\sin^2(\\theta)";
            break;
        default: // Kerr-Newman
            m[0] = "-\\frac{\\Delta - a^2 \\sin^2(\\theta)}{\\rho^2}";
            m[3] = m[12] = "-\\frac{a \\sin^2(\\theta)(r^2 + a^2 - \\Delta)}{\\rho^2}";
            m[5] = "\\frac{\\rho^2}{\\Delta}";
            m[10] = "\\rho^2";
            m[15] = "\\frac{(r^2 + a^2)^2 - a^2 \\Delta \\sin^2(\\theta)}{\\rho^2} \\sin^2(\\theta)";
            break;
        }

        std::string latex = "$\\begin{pmatrix}\n";
        for (int row = 0; row < 4; ++row)
        {
            for (int col = 0; col < 4; ++col)
            {
                latex += m[static_cast<std::size_t>(row * 4 + col)];
                if (col < 3) latex += " & ";
            }
            latex += row < 3 ? " \\\\\n" : "\n";
        }
        latex += "\\end{pmatrix}$";
        return latex;
    }

    struct GeneralRelativityWindowLayer final : public HotReloadableLayer
    {
        using Dependencies = std::tuple<Logger, Renderer, LatexRenderer, UiState>;

        GeneralRelativityWindowLayer(const std::shared_ptr<Logger>& logger, std::shared_ptr<Renderer> renderer,
                                     std::shared_ptr<LatexRenderer> latex, std::shared_ptr<UiState> uiState)
            : HotReloadableLayer(logger), m_Renderer(std::move(renderer)), m_Latex(std::move(latex)),
              m_UiState(std::move(uiState))
        {
        }

        void OnAttach() override
        {
            const auto device = m_Renderer->GetDevice();
            m_UploadPool = std::make_unique<VulkanCommandPool>(device, m_Logger, device->GetQueueIndices().Graphics);
        }

        void OnUiRender() override
        {
            if (!m_UiState->ShowGeneralRelativityWindow) return;
            ImGui::SetNextWindowSize(ImVec2(500, 600), ImGuiCond_FirstUseEver);
            if (!ImGui::Begin("General Relativity", &m_UiState->ShowGeneralRelativityWindow))
            {
                ImGui::End();
                return;
            }

            ImGui::TextWrapped(
                "Configure which exact solution to Einstein's field equations the raytracer uses.");
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            const char* metricNames[] = {"Schwarzschild", "Kerr", "Reissner-Nordstrom", "Kerr-Newman"};
            ImGui::Combo("Metric Type", &m_UiState->Render.MetricType, metricNames, IM_ARRAYSIZE(metricNames));

            ImGui::Spacing();
            ImGui::TextUnformatted("Metric Tensor (Boyer-Lindquist coordinates):");

            const auto device = m_Renderer->GetDevice();
            m_Formulas.Draw(*m_Latex, device, *m_UploadPool, device->GetGraphicsQueue(),
                           BuildMetricLatex(m_UiState->Render.MetricType));

            ImGui::End();
        }

    private:
        std::shared_ptr<Renderer> m_Renderer;
        std::shared_ptr<LatexRenderer> m_Latex;
        std::shared_ptr<UiState> m_UiState;
        std::unique_ptr<VulkanCommandPool> m_UploadPool;
        LatexFormulaView m_Formulas;
    };
}

GPP_DEFINE_HOT_RELOAD_LAYER(GeneralRelativityWindowLayer)
