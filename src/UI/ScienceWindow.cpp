#include "ScienceWindow.h"
#include "LatexRenderer.h"
#include "UI.h"
#include "imgui.h"
#include "imgui_internal.h"
#include <map>
#include <string>
#include <vector>
#include <functional>
#include <stack>

namespace ScienceWindow {

    // --------------------------------------------------------------------------------------------
    // Internal State
    // --------------------------------------------------------------------------------------------

    struct Page {
        std::string title;
        std::function<void()> content;
    };

    static std::map<std::string, Page> s_pages;
    static std::string s_currentPage = "Home";
    static std::vector<std::string> s_history;

    // --------------------------------------------------------------------------------------------
    // Graphic Helpers
    // --------------------------------------------------------------------------------------------

    static void RenderSectionHeader(const char* label) {
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(180.0f/255.0f, 100.0f/255.0f, 40.0f/255.0f, 1.0f));
        ImGui::Text("%s", label);
        ImGui::PopStyleColor();
        ImGui::Separator();
        ImGui::Spacing();
    }

    static void RenderFormula(const char* description, const char* latex, const int dpi = 150) {
        if (description && description[0]) {
            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "%s", description);
        }

        const LatexTexture& tex = LatexRenderer::Instance().RenderLatex(latex, dpi);
        if (tex.valid && tex.descriptorSet) {
            float scale = 0.8f;
            // Adjust scale if needed based on DPI
            ImGui::Image((ImTextureID)(VkDescriptorSet)tex.descriptorSet,
                         ImVec2(tex.width * scale, tex.height * scale));
        } else {
            ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "[Formula loading...] %s", latex);
        }
        ImGui::Spacing();
    }

    // --------------------------------------------------------------------------------------------
    // Navigation Helpers
    // --------------------------------------------------------------------------------------------

    static void GoToPage(const std::string& pageId) {
        if (s_pages.find(pageId) != s_pages.end()) {
            if (s_currentPage != pageId) {
                 // Push current page to history before switching
                s_history.push_back(s_currentPage);
                s_currentPage = pageId;
            }
        }
    }

    static void GoBack() {
        if (!s_history.empty()) {
            s_currentPage = s_history.back();
            s_history.pop_back();
        }
    }

    // Renders a clickable link (blue text button)
    // Use like: ImGui::Text("See also:"); ImGui::SameLine(); RenderLink("Black Holes", "BlackHoles");
    static void RenderLink(const char* label, const std::string& targetPageId) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.7f, 1.0f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0,0,0,0)); // Transparent background
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.4f, 0.7f, 1.0f, 0.1f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.4f, 0.7f, 1.0f, 0.2f));

        if (ImGui::SmallButton(label)) {
            GoToPage(targetPageId);
        }

        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Go to: %s", targetPageId.c_str());
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        }

        ImGui::PopStyleColor(4);
    }

    // Renders text followed by a link on the same line (if it fits, simplified)
    // Helps constructing sentences like "See [Link] for more."
    static void RenderTextWithLink(const char* preText, const char* linkLabel, const std::string& targetPageId, const char* postText = "") {
        if (preText && preText[0]) {
            ImGui::Text("%s", preText);
            ImGui::SameLine();
        }
        RenderLink(linkLabel, targetPageId);
        if (postText && postText[0]) {
            ImGui::SameLine();
            ImGui::Text("%s", postText);
        }
    }

    // --------------------------------------------------------------------------------------------
    // Page Content Definitions
    // --------------------------------------------------------------------------------------------

    static void InitPages() {
        if (!s_pages.empty()) return;

        // --- HOME ---
        s_pages["Home"] = { "Science Database", []() {
            ImGui::TextWrapped(
                "Welcome to the MoleHole Science Database.\n"
                "This interactive encyclopedia explains the physics simulation running in this application."
            );
            ImGui::Spacing();

            RenderSectionHeader("Topics");

            ImGui::Bullet(); RenderTextWithLink("Learn about ", "General Relativity", "GeneralRelativity", " and the math of gravity.");
            ImGui::Bullet(); RenderTextWithLink("Understand ", "Black Holes", "BlackHoles", ", the engines of our simulation.");
            ImGui::Bullet(); RenderTextWithLink("Study the ", "Schwarzschild Metric", "Schwarzschild", " (static black holes).");
            ImGui::Bullet(); RenderTextWithLink("Discover the ", "Kerr Metric", "Kerr", " (rotating black holes).");
            ImGui::Bullet(); RenderTextWithLink("Visualize ", "Accretion Disks", "Accretion", ".");
            ImGui::Bullet(); RenderTextWithLink("Reference ", "Mathematics", "Math", " used in rendering.");
        }};

        // --- GENERAL RELATIVITY ---
        s_pages["GeneralRelativity"] = { "General Relativity", []() {
            ImGui::TextWrapped(
                "General Relativity (GR) is the geometric theory of gravitation published by Albert Einstein in 1915.\n"
                "In GR, gravity is not a force, but a curvature of spacetime caused by mass and energy."
            );
            ImGui::Spacing();

            RenderFormula(
                "Einstein Field Equations",
                "$R_{\\mu\\nu} - \\frac{1}{2}Rg_{\\mu\\nu} + \\Lambda g_{\\mu\\nu} = \\frac{8\\pi G}{c^4} T_{\\mu\\nu}$"
            );

            ImGui::TextWrapped(
                "These equations relate the geometry of spacetime (left side) to the distribution of matter/energy (right side).\n"
                "Our simulation solves specific exact solutions to these equations."
            );

            ImGui::Spacing();
            ImGui::Text("Main solutions used:");
            ImGui::Bullet(); RenderLink("Schwarzschild Metric", "Schwarzschild");
            ImGui::Bullet(); RenderLink("Kerr Metric", "Kerr");
        }};

        // --- BLACK HOLES ---
        s_pages["BlackHoles"] = { "Black Holes", []() {
            ImGui::TextWrapped(
                "A black hole is a region of spacetime where gravity is so strong that nothing, including light, can escape.\n"
                "The boundary of no escape is called the Event Horizon."
            );
            ImGui::Spacing();

            RenderTextWithLink("For static black holes, see ", "Schwarzschild Metric", "Schwarzschild", ".");
            RenderTextWithLink("For rotating black holes, see ", "Kerr Metric", "Kerr", ".");

            RenderSectionHeader("Key Concepts");
            ImGui::BulletText("Event Horizon: The point of no return.");
            ImGui::BulletText("Singularity: The center where density becomes infinite.");
            ImGui::BulletText("Photon Sphere: Where light can orbit the black hole.");
            ImGui::BulletText("Accretion Disk: Matter spiraling in.");

            ImGui::Spacing();
            RenderLink("Back to Home", "Home");
        }};

        // --- SCHWARZSCHILD ---
        s_pages["Schwarzschild"] = { "Schwarzschild Metric", []() {
            ImGui::TextWrapped(
                "The Schwarzschild metric describes the gravitational field outside a spherical, non-rotating mass."
            );
            ImGui::Spacing();

            RenderFormula(
                "Schwarzschild Line Element",
                "$ds^2 = -\\left(1-\\frac{r_s}{r}\\right)c^2dt^2 + \\left(1-\\frac{r_s}{r}\\right)^{-1}dr^2 + r^2d\\Omega^2$"
            );

            ImGui::TextWrapped(
                "Where rs is the Schwarzschild radius:"
            );
            RenderFormula(
                nullptr,
                "$r_s = \\frac{2GM}{c^2}$"
            );

            ImGui::TextWrapped("Key Features:");
            ImGui::BulletText("Event Horizon at r = rs");
            ImGui::BulletText("Photon Sphere at r = 1.5 rs");
            ImGui::BulletText("Innermost Stable Circular Orbit (ISCO) at r = 3 rs");

            ImGui::Spacing();
            RenderTextWithLink("Compare with ", "Kerr Metric", "Kerr", ".");
        }};

        // --- KERR ---
        s_pages["Kerr"] = { "Kerr Metric", []() {
            ImGui::TextWrapped(
                "The Kerr metric describes the geometry of empty spacetime around a rotating uncharged axially-symmetric black hole."
            );
            ImGui::Spacing();

            RenderFormula(
                "Kerr Line Element (Boyer-Lindquist coordinates)",
                "$ds^2 = -\\left(1-\\frac{r_s r}{\\Sigma}\\right)c^2dt^2 + \\frac{\\Sigma}{\\Delta}dr^2 + \\Sigma d\\theta^2 + ...$"
            );

            ImGui::TextWrapped("Where 'a' represents the spin parameter (angular momentum per unit mass).");

            RenderSectionHeader("Frame Dragging");
            ImGui::TextWrapped(
                "Rotation of the black hole 'drags' spacetime with it. This creates a region called the Ergosphere "
                "outside the event horizon where it is impossible to stand still."
            );

            ImGui::Spacing();
            RenderTextWithLink("See ", "General Relativity", "GeneralRelativity", " for the underlying theory.");
        }};

        // --- ACCRETION ---
        s_pages["Accretion"] = { "Accretion Disks", []() {
            ImGui::TextWrapped(
                "An accretion disk is a structure formed by diffuse material in orbital motion around a massive central body."
                "Friction causes the material to spiral inward and heat up, emitting radiation."
            );

            RenderSectionHeader("Visual Appearance");
            ImGui::BulletText("Doppler Beaming: One side appears brighter because it moves towards the observer.");
            ImGui::BulletText("Gravitational Lensing: The back of the disk is visible above/below the black hole.");

            ImGui::Spacing();
            RenderLink("Back to Black Holes", "BlackHoles");
        }};

        // --- MATHEMATICS ---
        s_pages["Math"] = { "Mathematics Reference", []() {
            ImGui::TextWrapped("Common mathematical concepts used in physics rendering.");

            RenderSectionHeader("Integrals");
            RenderFormula(
                "Gaussian Integral",
                "$\\int_0^\\infty e^{-x^2}\\,dx = \\frac{\\sqrt{\\pi}}{2}$"
            );

            RenderSectionHeader("Topology");
            RenderFormula(
                "Gauss-Bonnet Theorem",
                "$\\int_M K\\,dA + \\int_{\\partial M} k_g\\,ds = 2\\pi\\chi(M)$"
            );
        }};
    }

    // --------------------------------------------------------------------------------------------
    // Main Render Function
    // --------------------------------------------------------------------------------------------

    void Render(UI* ui) {
        if (!ImGui::Begin("Science")) {
            ImGui::End();
            return;
        }

        // Initialize systems
        LatexRenderer::Instance().Init();
        InitPages();

        // 1. Navigation Bar
        {
            if (ImGui::Button(" < Back ")) {
                GoBack();
            }
            if (ImGui::IsItemHovered() && !s_history.empty()) {
                ImGui::SetTooltip("Go back to %s", s_history.back().c_str());
            }
            if (s_history.empty()) {
                ImGui::SetItemTooltip("History empty");
            }

            ImGui::SameLine();
            if (ImGui::Button(" Home ")) {
                GoToPage("Home");
            }

            ImGui::SameLine();
            ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
            ImGui::SameLine();

            // Current Page Title
            if (s_pages.find(s_currentPage) != s_pages.end()) {
                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "%s", s_pages[s_currentPage].title.c_str());
            } else {
                ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "Page Not Found: %s", s_currentPage.c_str());
            }
        }

        ImGui::Separator();

        // 2. Page Content
        ImGui::BeginChild("SciencePageContent");

        auto it = s_pages.find(s_currentPage);
        if (it != s_pages.end()) {
            // Render the page's content lambda
            it->second.content();
        } else {
            ImGui::Text("Error: Page '%s' does not exist.", s_currentPage.c_str());
            if (ImGui::Button("Return to Home")) {
                s_currentPage = "Home";
                s_history.clear();
            }
        }

        ImGui::EndChild();

        ImGui::End();
    }
}

