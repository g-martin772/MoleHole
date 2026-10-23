export module MoleHole:Physics.Tidal;

import std;
import :Rendering.Metric;
import :Physics.Geodesic;
import :Physics.Observer;

// The tidal tensor E_ij = R_(i u j u) of the Kerr–Newman family for any observer, G = c = 1, Boyer–Lindquist
// coordinates. Geodesic deviation: d²ξ^i/dτ² = -E^i_j ξ^j, so a negative eigenvalue stretches and a positive one
// squeezes. In Schwarzschild a static observer sees diag(-2M/r³, M/r³, M/r³) along (r̂, θ̂, φ̂).
//
// No derivatives of the metric are taken. The curvature is written down exactly in Carter's orthonormal frame, where
// the Weyl part is the type-D form with Ψ₂ and the Ricci part is the stress of the radial electromagnetic field, and
// then Lorentz transformed to the observer. That stays accurate in the throat of a near-extremal hole, where the
// coordinate metric loses all precision. Background and sources: docs/notes/tidal_forces.md
export namespace MoleHole
{
    using Matrix3 = std::array<Vector3, 3>;
    using Matrix4 = std::array<Vector4, 4>;

    // R_abcd in an orthonormal frame, index order (t̂, 1̂, 2̂, 3̂), signature (-+++).
    using FrameRiemann = std::array<std::array<std::array<std::array<double, 4>, 4>, 4>, 4>;

    // Velocity of Carter's frame relative to the ZAMO, along φ̂: v = a sinθ √Δ / (r² + a²). Carter's observer
    // circles the axis with Ω = a / (r² + a²), and its frame shares e_r̂ and e_θ̂ with the ZAMO.
    [[nodiscard]] inline double CarterFrameVelocity(const KerrNewmanParams& p, const double r, const double theta)
    {
        const double a = p.SpinParameter;
        return a * std::sin(theta) * std::sqrt(std::max(HorizonDelta(p, r), 0.0)) / (r * r + a * a);
    }

    // The Riemann tensor in Carter's frame. Weyl part from Ψ₂ = -M/(r - i a cosθ)³ + Q²/((r + i a cosθ)(r - i a cosθ)³)
    // with E - iB = Ψ₂ diag(2, -1, -1) along (r̂, θ̂, φ̂), B as defined by C_0ijk = ε_jkl B_li below. The literature
    // disagrees on the sign of B; this one is fixed against the coordinate Riemann tensor (tidal_tests.cpp).
    // Ricci part R_ab = (E_r² + B_r²) diag(1, -1, 1, 1) from the radial field E_r + i B_r = Q / (r - i a cosθ)².
    [[nodiscard]] inline FrameRiemann CarterFrameRiemann(const KerrNewmanParams& p, const double r, const double theta)
    {
        const std::complex<double> rho{r, -p.SpinParameter * std::cos(theta)};
        const double q2 = p.Charge * p.Charge;
        const std::complex<double> psi2 = -p.Mass / (rho * rho * rho) + q2 / (std::conj(rho) * rho * rho * rho);
        const double fieldEnergy = q2 / std::norm(rho * rho);

        const Vector3 weight{2.0, -1.0, -1.0};
        Matrix3 electric{};
        Matrix3 magnetic{};
        for (std::size_t i = 0; i < 3; ++i)
        {
            electric[i][i] = weight[i] * psi2.real();
            magnetic[i][i] = -weight[i] * psi2.imag();
        }

        constexpr auto levi = [](const std::size_t i, const std::size_t j, const std::size_t k)
        {
            return static_cast<double>((static_cast<int>(j) - static_cast<int>(i)) * (static_cast<int>(k) - static_cast<int>(i))
                                       * (static_cast<int>(k) - static_cast<int>(j))) / 2.0;
        };
        const Vector4 eta{-1.0, 1.0, 1.0, 1.0};
        const Vector4 ricci{fieldEnergy, -fieldEnergy, fieldEnergy, fieldEnergy};

        FrameRiemann riemann{};
        // Weyl: C_0i0j = E_ij, C_0ijk = ε_jkl B_li, C_ijkl = -ε_ijp ε_klq E_pq; the other orderings follow from the
        // symmetries R_abcd = -R_bacd = -R_abdc = R_cdab.
        const auto set = [&](const std::size_t a, const std::size_t b, const std::size_t c, const std::size_t d, const double value)
        {
            riemann[a][b][c][d] = value;
            riemann[b][a][c][d] = -value;
            riemann[a][b][d][c] = -value;
            riemann[b][a][d][c] = value;
            riemann[c][d][a][b] = value;
            riemann[d][c][a][b] = -value;
            riemann[c][d][b][a] = -value;
            riemann[d][c][b][a] = value;
        };
        for (std::size_t i = 0; i < 3; ++i)
        {
            for (std::size_t j = 0; j < 3; ++j)
            {
                set(0, i + 1, 0, j + 1, electric[i][j]);
                for (std::size_t k = 0; k < 3; ++k)
                {
                    double mixed = 0.0;
                    for (std::size_t l = 0; l < 3; ++l) mixed += levi(j, k, l) * magnetic[l][i];
                    if (mixed != 0.0) set(0, i + 1, j + 1, k + 1, mixed);
                }
            }
        }
        for (std::size_t i = 0; i < 3; ++i)
        {
            for (std::size_t j = i + 1; j < 3; ++j)
            {
                for (std::size_t k = 0; k < 3; ++k)
                {
                    for (std::size_t l = k + 1; l < 3; ++l)
                    {
                        double spatial = 0.0;
                        for (std::size_t m = 0; m < 3; ++m)
                        {
                            for (std::size_t n = 0; n < 3; ++n) spatial -= levi(i, j, m) * levi(k, l, n) * electric[m][n];
                        }
                        if (spatial != 0.0) set(i + 1, j + 1, k + 1, l + 1, spatial);
                    }
                }
            }
        }
        // Ricci part (R = 0 for the Maxwell field): ½ (η_ac R_bd - η_ad R_bc - η_bc R_ad + η_bd R_ac)
        if (fieldEnergy != 0.0)
        {
            for (std::size_t a = 0; a < 4; ++a)
            {
                for (std::size_t b = 0; b < 4; ++b)
                {
                    if (a == b) continue;
                    // Only R_abab and R_abba get a diagonal Ricci contribution
                    const double value = 0.5 * (eta[a] * ricci[b] + eta[b] * ricci[a]);
                    riemann[a][b][a][b] += value;
                    riemann[a][b][b][a] -= value;
                }
            }
        }
        return riemann;
    }

    // Orthonormal frame of an observer moving with velocity v through a reference frame, as rows of components in that
    // frame: row 0 = γ (1, v), row j = (γ v_j, δ_jk + (γ - 1) v_j v_k / v²). The matrix form of ObserverTetrad.
    [[nodiscard]] inline Matrix4 BoostMatrix(const Vector3& velocity)
    {
        const double gamma = LorentzFactor(velocity);
        const double boost = gamma * gamma / (gamma + 1.0);
        Matrix4 result{};
        result[0][0] = gamma;
        for (std::size_t j = 0; j < 3; ++j)
        {
            result[0][j + 1] = gamma * velocity[j];
            result[j + 1][0] = gamma * velocity[j];
            for (std::size_t k = 0; k < 3; ++k)
            {
                result[j + 1][k + 1] = (j == k ? 1.0 : 0.0) + boost * velocity[j] * velocity[k];
            }
        }
        return result;
    }

    // E_ij = R_abcd e_i^a u^b e_j^c u^d for a frame given as rows of components in the frame `riemann` is written in.
    [[nodiscard]] inline Matrix3 ContractTidal(const FrameRiemann& riemann, const Matrix4& frame)
    {
        Matrix3 result{};
        for (std::size_t i = 0; i < 3; ++i)
        {
            for (std::size_t j = i; j < 3; ++j)
            {
                double sum = 0.0;
                for (std::size_t a = 0; a < 4; ++a)
                {
                    for (std::size_t b = 0; b < 4; ++b)
                    {
                        for (std::size_t c = 0; c < 4; ++c)
                        {
                            for (std::size_t d = 0; d < 4; ++d)
                            {
                                sum += riemann[a][b][c][d] * frame[i + 1][a] * frame[0][b] * frame[j + 1][c] * frame[0][d];
                            }
                        }
                    }
                }
                result[i][j] = result[j][i] = sum;
            }
        }
        return result;
    }

    // Tidal tensor (in 1/M²; times c²/r_g² for SI, see TidalTensorSi) of an observer moving with velocity v relative
    // to the ZAMO at (r, theta). Components along the observer's own axes, the boosted ZAMO axes of ObserverTetrad,
    // ordered (r̂, θ̂, φ̂).
    [[nodiscard]] inline Matrix3 TidalTensor(const KerrNewmanParams& p, const double r, const double theta,
                                             const Vector3& velocity)
    {
        // Observer in ZAMO components, re-expressed in Carter's frame: the inverse of a boost is the boost by -v.
        const Matrix4 observer = BoostMatrix(velocity);
        const Matrix4 zamoInCarter = BoostMatrix(Vector3{0.0, 0.0, -CarterFrameVelocity(p, r, theta)});
        Matrix4 frame{};
        for (std::size_t a = 0; a < 4; ++a)
        {
            for (std::size_t n = 0; n < 4; ++n)
            {
                double sum = 0.0;
                for (std::size_t m = 0; m < 4; ++m) sum += observer[a][m] * zamoInCarter[m][n];
                frame[a][n] = sum;
            }
        }
        return ContractTidal(CarterFrameRiemann(p, r, theta), frame);
    }

    // Tidal tensor along a timelike geodesic, in the boosted ZAMO axes of its momentary velocity (ZamoVelocity).
    [[nodiscard]] inline Matrix3 TidalTensor(const KerrNewmanParams& p, const GeodesicState<double>& state)
    {
        return TidalTensor(p, state.R, state.Theta, ZamoVelocity(p, state));
    }

    // From 1/M² to s⁻² for a hole whose gravitational radius GM/c² is `lengthM`: c² / r_g².
    [[nodiscard]] inline Matrix3 TidalTensorSi(const Matrix3& geometric, const double lengthM, const double speedOfLightMs)
    {
        const double scale = speedOfLightMs * speedOfLightMs / (lengthM * lengthM);
        Matrix3 result{};
        for (std::size_t i = 0; i < 3; ++i)
        {
            for (std::size_t j = 0; j < 3; ++j) result[i][j] = geometric[i][j] * scale;
        }
        return result;
    }

    struct TidalAxes
    {
        Vector3 Eigenvalues{}; // ascending: [0] is the strongest stretch, [2] the strongest squeeze
        Matrix3 Axes{};        // Axes[k] is the unit eigenvector of Eigenvalues[k]
    };

    // Principal axes of a symmetric 3×3 tensor by cyclic Jacobi rotations; converges to double precision in a few
    // sweeps for a 3×3.
    [[nodiscard]] inline TidalAxes PrincipalAxes(const Matrix3& tensor)
    {
        Matrix3 m = tensor;
        Matrix3 v{Vector3{1.0, 0.0, 0.0}, Vector3{0.0, 1.0, 0.0}, Vector3{0.0, 0.0, 1.0}};
        for (int sweep = 0; sweep < 32; ++sweep)
        {
            const double off = m[0][1] * m[0][1] + m[0][2] * m[0][2] + m[1][2] * m[1][2];
            const double diagonal = m[0][0] * m[0][0] + m[1][1] * m[1][1] + m[2][2] * m[2][2];
            if (off <= 1e-32 * diagonal || off == 0.0) break;
            for (std::size_t p = 0; p < 2; ++p)
            {
                for (std::size_t q = p + 1; q < 3; ++q)
                {
                    if (m[p][q] == 0.0) continue;
                    const double theta = (m[q][q] - m[p][p]) / (2.0 * m[p][q]);
                    const double t = (theta >= 0.0 ? 1.0 : -1.0) / (std::abs(theta) + std::sqrt(theta * theta + 1.0));
                    const double c = 1.0 / std::sqrt(t * t + 1.0);
                    const double s = t * c;
                    for (std::size_t k = 0; k < 3; ++k)
                    {
                        const double mkp = m[k][p];
                        const double mkq = m[k][q];
                        m[k][p] = c * mkp - s * mkq;
                        m[k][q] = s * mkp + c * mkq;
                    }
                    for (std::size_t k = 0; k < 3; ++k)
                    {
                        const double mpk = m[p][k];
                        const double mqk = m[q][k];
                        m[p][k] = c * mpk - s * mqk;
                        m[q][k] = s * mpk + c * mqk;
                    }
                    for (std::size_t k = 0; k < 3; ++k)
                    {
                        const double vkp = v[k][p];
                        const double vkq = v[k][q];
                        v[k][p] = c * vkp - s * vkq;
                        v[k][q] = s * vkp + c * vkq;
                    }
                }
            }
        }
        std::array<std::size_t, 3> order{0, 1, 2};
        std::ranges::sort(order, [&](const std::size_t x, const std::size_t y) { return m[x][x] < m[y][y]; });
        TidalAxes result{};
        for (std::size_t k = 0; k < 3; ++k)
        {
            result.Eigenvalues[k] = m[order[k]][order[k]];
            for (std::size_t i = 0; i < 3; ++i) result.Axes[k][i] = v[i][order[k]];
        }
        return result;
    }
}
