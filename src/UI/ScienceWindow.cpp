#include "ScienceWindow.h"
#include "LatexRenderer.h"

#include "../Application/UI.h"
#include "imgui.h"

static void RenderSectionHeader(const char* label) {
    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(180.0f/255.0f, 100.0f/255.0f, 40.0f/255.0f, 1.0f));
    ImGui::Text("%s", label);
    ImGui::PopStyleColor();
    ImGui::Separator();
    ImGui::Spacing();
}

static void RenderFormula(const char* description, const char* latex, const int dpi = 200) {
    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "%s", description);

    const LatexTexture& tex = LatexRenderer::Instance().RenderLatex(latex, dpi);
    if (tex.valid && tex.textureID) {
        float scale = 1.0f;
        ImGui::Image(static_cast<ImTextureID>(static_cast<uintptr_t>(tex.textureID)),
                     ImVec2(tex.width * scale, tex.height * scale));
    } else {
        ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "[LaTeX unavailable] %s", latex);
    }
    ImGui::Spacing();
}

void ScienceWindow::Render(UI* ui) {
    if (!ImGui::Begin("Science")) {
        ImGui::End();
        return;
    }

    LatexRenderer::Instance().Init();

    RenderSectionHeader("Integral");

    RenderFormula(
        "Gaussian Integral",
        "$\\displaystyle \\int_0^\\infty e^{-x^2}\\,dx = \\frac{\\sqrt{\\pi}}{2}$"
    );

    RenderFormula(
        "Gauss-Bonnet Theorem",
        "$\\displaystyle \\int_M K\\,dA + \\int_{\\partial M} k_g\\,ds = 2\\pi\\chi(M)$"
    );

    RenderSectionHeader("Limit");

    RenderFormula(
        "Definition of the Derivative",
        "$\\displaystyle f'(x) = \\lim_{h \\to 0} \\frac{f(x+h) - f(x)}{h}$"
    );

    RenderFormula(
        "Euler's Limit",
        "$\\displaystyle e = \\lim_{n \\to \\infty} \\left(1 + \\frac{1}{n}\\right)^{\\!n}$"
    );

    RenderSectionHeader("Partial Differential");

    RenderFormula(
        "Wave Equation",
        "$\\displaystyle \\frac{\\partial^2 u}{\\partial t^2} = c^2 \\nabla^2 u$"
    );

    RenderFormula(
        "Einstein Field Equations",
        "$\\displaystyle G_{\\mu\\nu} + \\Lambda g_{\\mu\\nu} = \\frac{8\\pi G}{c^4}\\,T_{\\mu\\nu}$"
    );

    RenderSectionHeader("Vectors & Matrices");

    RenderFormula(
        "Cross Product",
        "$\\displaystyle \\vec{a} \\times \\vec{b} = \\begin{vmatrix} \\hat{i} & \\hat{j} & \\hat{k} \\\\ a_1 & a_2 & a_3 \\\\ b_1 & b_2 & b_3 \\end{vmatrix}$"
    );

    RenderFormula(
        "Geodesic Equation",
        "$\\displaystyle \\frac{d^2 x^\\mu}{d\\tau^2} + \\Gamma^\\mu_{\\alpha\\beta}\\,\\frac{dx^\\alpha}{d\\tau}\\frac{dx^\\beta}{d\\tau} = 0$"
    );

    RenderSectionHeader("Black Hole Physics");

    RenderFormula(
        "Schwarzschild Radius",
        "$\\displaystyle r_s = \\frac{2GM}{c^2}$"
    );

    RenderFormula(
        "Kerr Metric (Boyer-Lindquist)",
        "$\\displaystyle ds^2 = -\\left(1-\\frac{r_s r}{\\Sigma}\\right)c^2 dt^2 "
        "+ \\frac{\\Sigma}{\\Delta}dr^2 + \\Sigma\\,d\\theta^2 "
        "+ \\left(r^2+a^2+\\frac{r_s r a^2}{\\Sigma}\\sin^2\\theta\\right)\\sin^2\\theta\\,d\\phi^2 "
        "- \\frac{2r_s r a \\sin^2\\theta}{\\Sigma}\\,c\\,dt\\,d\\phi$"
    );

    RenderFormula(
        "Hawking Temperature",
        "$\\displaystyle T_H = \\frac{\\hbar c^3}{8\\pi G M k_B}$"
    );

    ImGui::End();
}

