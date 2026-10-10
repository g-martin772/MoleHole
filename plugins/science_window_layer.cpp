import GPP;
import MoleHole;
import std;

#include <gpp/hot_reload_export.h>

using namespace GPP;
using namespace MoleHole;

namespace
{
    struct ScienceWindowLayer final : public HotReloadableLayer
    {
        using Dependencies = std::tuple<Logger, Renderer, LatexRenderer, UiState>;

        ScienceWindowLayer(const std::shared_ptr<Logger>& logger, std::shared_ptr<Renderer> renderer,
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
            if (!m_UiState->ShowScienceWindow) return;
            ImGui::SetNextWindowSize(ImVec2(520, 600), ImGuiCond_FirstUseEver);
            if (!ImGui::Begin("Science", &m_UiState->ShowScienceWindow))
            {
                ImGui::End();
                return;
            }

            if (ImGui::Button(" < Back "))
            {
                GoBack();
            }
            if (ImGui::IsItemHovered() && !m_History.empty())
            {
                ImGui::SetTooltip("Go back to %s", m_History.back().c_str());
            }
            ImGui::SameLine();
            if (ImGui::Button(" Home "))
            {
                GoToPage("Home");
            }
            ImGui::SameLine();
            ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "%s", PageTitle(m_CurrentPage));

            ImGui::Separator();
            ImGui::BeginChild("SciencePageContent");
            RenderPage(m_CurrentPage);
            ImGui::EndChild();

            ImGui::End();
        }

    private:
        void GoToPage(const std::string& pageId)
        {
            if (pageId != m_CurrentPage)
            {
                m_History.push_back(m_CurrentPage);
                m_CurrentPage = pageId;
            }
        }

        void GoBack()
        {
            if (!m_History.empty())
            {
                m_CurrentPage = m_History.back();
                m_History.pop_back();
            }
        }

        void Link(const char* label, const std::string& targetPageId)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.7f, 1.0f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.4f, 0.7f, 1.0f, 0.1f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.4f, 0.7f, 1.0f, 0.2f));
            if (ImGui::SmallButton(label)) GoToPage(targetPageId);
            ImGui::PopStyleColor(4);
        }

        void TextWithLink(const char* pre, const char* label, const std::string& target,
                          const char* post = "")
        {
            if (pre && pre[0])
            {
                ImGui::TextUnformatted(pre);
                ImGui::SameLine();
            }
            Link(label, target);
            if (post && post[0])
            {
                ImGui::SameLine();
                ImGui::TextUnformatted(post);
            }
        }

        void Formula(const char* description, const char* latex)
        {
            if (description && description[0])
            {
                ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "%s", description);
            }
            const auto device = m_Renderer->GetDevice();
            m_Formulas.Draw(*m_Latex, device, *m_UploadPool, device->GetGraphicsQueue(), latex);
            ImGui::Spacing();
        }

        static void Header(const char* label)
        {
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(180.0f / 255.0f, 100.0f / 255.0f, 40.0f / 255.0f, 1.0f));
            ImGui::TextUnformatted(label);
            ImGui::PopStyleColor();
            ImGui::Separator();
            ImGui::Spacing();
        }

        static const char* PageTitle(const std::string& page)
        {
            if (page == "Home") return "Science Database";
            if (page == "GeneralRelativity") return "General Relativity";
            if (page == "BlackHoles") return "Black Holes";
            if (page == "Schwarzschild") return "Schwarzschild Metric";
            if (page == "Kerr") return "Kerr Metric";
            if (page == "Accretion") return "Accretion Disks";
            if (page == "Math") return "Mathematics Reference";
            return "Unknown Page";
        }

        void RenderPage(const std::string& page)
        {
            if (page == "Home")
            {
                ImGui::TextWrapped(
                    "Welcome to the MoleHole Science Database. This interactive encyclopedia "
                    "explains the physics simulation running in this application.");
                Header("Topics");
                ImGui::Bullet();
                TextWithLink("Learn about ", "General Relativity", "GeneralRelativity", " and the math of gravity.");
                ImGui::Bullet();
                TextWithLink("Understand ", "Black Holes", "BlackHoles", ", the engines of our simulation.");
                ImGui::Bullet();
                TextWithLink("Study the ", "Schwarzschild Metric", "Schwarzschild", " (static black holes).");
                ImGui::Bullet();
                TextWithLink("Discover the ", "Kerr Metric", "Kerr", " (rotating black holes).");
                ImGui::Bullet();
                TextWithLink("Visualize ", "Accretion Disks", "Accretion", ".");
                ImGui::Bullet();
                TextWithLink("Reference ", "Mathematics", "Math", " used in rendering.");
            }
            else if (page == "GeneralRelativity")
            {
                ImGui::TextWrapped(
                    "General Relativity (GR) is the geometric theory of gravitation published by "
                    "Albert Einstein in 1915. In GR, gravity is not a force, but a curvature of "
                    "spacetime caused by mass and energy.");
                Formula("Einstein Field Equations",
                       "$R_{\\mu\\nu} - \\frac{1}{2}Rg_{\\mu\\nu} + \\Lambda g_{\\mu\\nu} = "
                       "\\frac{8\\pi G}{c^4} T_{\\mu\\nu}$");
                ImGui::TextWrapped(
                    "These equations relate the geometry of spacetime (left side) to the "
                    "distribution of matter/energy (right side). Our simulation solves specific "
                    "exact solutions to these equations.");
                ImGui::Spacing();
                ImGui::TextUnformatted("Main solutions used:");
                ImGui::Bullet();
                Link("Schwarzschild Metric", "Schwarzschild");
                ImGui::Bullet();
                Link("Kerr Metric", "Kerr");
            }
            else if (page == "BlackHoles")
            {
                ImGui::TextWrapped(
                    "A black hole is a region of spacetime where gravity is so strong that "
                    "nothing, including light, can escape. The boundary of no escape is called "
                    "the Event Horizon.");
                ImGui::Spacing();
                TextWithLink("For static black holes, see ", "Schwarzschild Metric", "Schwarzschild", ".");
                TextWithLink("For rotating black holes, see ", "Kerr Metric", "Kerr", ".");
                Header("Key Concepts");
                ImGui::BulletText("Event Horizon: The point of no return.");
                ImGui::BulletText("Singularity: The center where density becomes infinite.");
                ImGui::BulletText("Photon Sphere: Where light can orbit the black hole.");
                ImGui::BulletText("Accretion Disk: Matter spiraling in.");
                ImGui::Spacing();
                Link("Back to Home", "Home");
            }
            else if (page == "Schwarzschild")
            {
                ImGui::TextWrapped(
                    "The Schwarzschild metric describes the gravitational field outside a "
                    "spherical, non-rotating mass.");
                Formula("Schwarzschild Line Element",
                       "$ds^2 = -\\left(1-\\frac{r_s}{r}\\right)c^2dt^2 + "
                       "\\left(1-\\frac{r_s}{r}\\right)^{-1}dr^2 + r^2d\\Omega^2$");
                ImGui::TextWrapped("Where rs is the Schwarzschild radius:");
                Formula(nullptr, "$r_s = \\frac{2GM}{c^2}$");
                ImGui::TextWrapped("Key Features:");
                ImGui::BulletText("Event Horizon at r = rs");
                ImGui::BulletText("Photon Sphere at r = 1.5 rs");
                ImGui::BulletText("Innermost Stable Circular Orbit (ISCO) at r = 3 rs");
                ImGui::Spacing();
                TextWithLink("Compare with ", "Kerr Metric", "Kerr", ".");
            }
            else if (page == "Kerr")
            {
                ImGui::TextWrapped(
                    "The Kerr metric describes the geometry of empty spacetime around a "
                    "rotating uncharged axially-symmetric black hole.");
                Formula("Kerr Line Element (Boyer-Lindquist coordinates)",
                       "$ds^2 = -\\left(1-\\frac{r_s r}{\\Sigma}\\right)c^2dt^2 + "
                       "\\frac{\\Sigma}{\\Delta}dr^2 + \\Sigma d\\theta^2 + ...$");
                ImGui::TextWrapped("Where 'a' represents the spin parameter (angular momentum per unit mass).");
                Header("Frame Dragging");
                ImGui::TextWrapped(
                    "Rotation of the black hole 'drags' spacetime with it. This creates a region "
                    "called the Ergosphere outside the event horizon where it is impossible to "
                    "stand still.");
                ImGui::Spacing();
                TextWithLink("See ", "General Relativity", "GeneralRelativity", " for the underlying theory.");
            }
            else if (page == "Accretion")
            {
                ImGui::TextWrapped(
                    "An accretion disk is a structure formed by diffuse material in orbital "
                    "motion around a massive central body. Friction causes the material to "
                    "spiral inward and heat up, emitting radiation.");
                Header("Visual Appearance");
                ImGui::BulletText("Doppler Beaming: One side appears brighter because it moves towards the observer.");
                ImGui::BulletText("Gravitational Lensing: The back of the disk is visible above/below the black hole.");
                ImGui::Spacing();
                Link("Back to Black Holes", "BlackHoles");
            }
            else if (page == "Math")
            {
                ImGui::TextWrapped("Common mathematical concepts used in physics rendering.");
                Header("Integrals");
                Formula("Gaussian Integral", "$\\int_0^\\infty e^{-x^2}\\,dx = \\frac{\\sqrt{\\pi}}{2}$");
                Header("Topology");
                Formula("Gauss-Bonnet Theorem",
                       "$\\int_M K\\,dA + \\int_{\\partial M} k_g\\,ds = 2\\pi\\chi(M)$");
            }
            else
            {
                ImGui::Text("Error: page '%s' does not exist.", page.c_str());
                if (ImGui::Button("Return to Home"))
                {
                    m_CurrentPage = "Home";
                    m_History.clear();
                }
            }
        }

        std::shared_ptr<Renderer> m_Renderer;
        std::shared_ptr<LatexRenderer> m_Latex;
        std::shared_ptr<UiState> m_UiState;
        std::unique_ptr<VulkanCommandPool> m_UploadPool;
        LatexFormulaView m_Formulas;
        std::string m_CurrentPage = "Home";
        std::vector<std::string> m_History;
    };
}

GPP_DEFINE_HOT_RELOAD_LAYER(ScienceWindowLayer)
