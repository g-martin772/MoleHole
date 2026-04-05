//
// Created by leo on 3/25/26.
//

#include "IntegrationMethods.h"
#include <boost/numeric/odeint/stepper/runge_kutta4.hpp>
#include <vector>

#include "glm/ext/vector_reciprocal.inl"
#include "spdlog/spdlog.h"

namespace
{
    void define_system(const std::vector<double> &x, std::vector<double> &dxdt, double t)
    {
        dxdt[0] = x[1];
        dxdt[1] = -x[0];
    }

    void schwarschild_system(const std::vector<double> &x, std::vector<double> &dxdt, double t)
    {
        constexpr double M = 1.0;
        constexpr double G = 1.0;

        // velocities
        dxdt[0] = x[4];
        dxdt[1] = x[5];
        dxdt[2] = x[6];
        dxdt[3] = x[7];

        // accelerations
        dxdt[4] = (2 * G * M * dxdt[1] * dxdt[0]) / (2 * G * M * x[1] - pow(x[1], 2));
        dxdt[5] = (pow(x[1] - 2 * G * M, 2) *
            (G * M * pow(dxdt[0], 2) - pow(x[1], 3) * (pow(dxdt[2], 2) + pow(sin(x[2]), 2) * pow(dxdt[3], 2)))
            - G * M * pow(x[1], 2) * pow(dxdt[1], 2)) / (pow(x[1], 3) * (2 * G * M - x[1]));
        dxdt[6] = sin(x[2]) * cos(x[2]) * pow(dxdt[3], 2) - (2 * dxdt[2] * dxdt[1]) / x[1];
        dxdt[7] = -(2 * dxdt[3] * (dxdt[1] + x[1] * dxdt[2] * (cos(x[2] / sin(x[2]))))) / x[1];
    }
}

void num_solve()
{
    using state_type = std::vector<double>;
    // initial condition
    state_type state(8);
    state[0] = 0.0;
    state[1] = 1000.0;
    state[2] = 3.14159 / 2.0;
    state[3] = 0.0;
    state[4] = 1.0;
    state[5] = 0.0;
    state[6] = 0.0;
    state[7] = 0.0;

    // define algorithm
    boost::numeric::odeint::runge_kutta4<std::vector<double>> rk4;

    double t = 0.0;
    constexpr int total_time = 100;
    int iterations = 0;

    while (t < total_time)
    {
        constexpr double dt = 0.001;
        rk4.do_step(schwarschild_system, state, t, dt);
        t += dt;
        spdlog::info("New State - t-Pos: {}, r-Pos: {}, theta-Pos: {}, phi-Pos: {},", state[0], state[1], state[2], state[3]);
        iterations++;
    }
    spdlog::info("Iterations: {}", iterations);
}