module MoleHole;

import :Rendering.Lut;
import std;
import vulkan;
import GPP;

using namespace GPP;

namespace MoleHole
{
    namespace
    {
        std::shared_ptr<VulkanImage> UploadRgba32f(const std::shared_ptr<VulkanDevice>& device,
                                                    VulkanCommandPool& uploadPool, vk::Queue queue,
                                                    std::uint32_t width, std::uint32_t height,
                                                    const std::vector<float>& rgbaData,
                                                    const std::string& debugName,
                                                    const std::shared_ptr<Logger>& logger)
        {
            const auto extent = vk::Extent3D{width, height, 1};
            const auto byteSize = static_cast<vk::DeviceSize>(rgbaData.size() * sizeof(float));

            VulkanBuffer staging(device, MakeStagingBufferSpecification(byteSize), logger);
            staging.Upload(rgbaData.data(), byteSize);

            auto image = std::make_shared<VulkanImage>(
                device,
                VulkanImageSpecification{
                    .extent = extent,
                    .format = vk::Format::eR32G32B32A32Sfloat,
                    .usage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst,
                    .aspectMask = vk::ImageAspectFlagBits::eColor,
                    .createSampler = true,
                    .samplerFilter = vk::Filter::eLinear,
                    .samplerAddressMode = vk::SamplerAddressMode::eClampToEdge,
                    .debugName = debugName
                },
                logger);

            ImmediateSubmit(uploadPool, queue, [&](const vk::CommandBuffer cmd)
            {
                TransitionImageLayout(cmd, image->GetImage(), vk::Format::eR32G32B32A32Sfloat,
                                      vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal);
                CopyBufferToImage(cmd, staging.GetBuffer(), image->GetImage(), extent);
                TransitionImageLayout(cmd, image->GetImage(), vk::Format::eR32G32B32A32Sfloat,
                                      vk::ImageLayout::eTransferDstOptimal,
                                      vk::ImageLayout::eShaderReadOnlyOptimal);
            });
            return image;
        }

        constexpr float kTempMin = 1000.0f;
        constexpr float kTempMax = 40000.0f;
        constexpr float kRedshiftMin = 0.1f;
        constexpr float kRedshiftMax = 3.0f;
        constexpr int kBlackbodyWidth = 256;
        constexpr int kBlackbodyHeight = 128;

        constexpr float kLightSpeed = 2.99792458e8f;
        constexpr float kBoltzmannConstant = 1.3806504e-23f;
        constexpr float kPlanckConstant = 6.62607015e-34f;
        constexpr float kMinCy = 3931191.541677483f;
        constexpr float kMaxCy = 9.15738182436502e16f;

        const std::vector<float> kMatchingFunctionsX = {
            0.0014f, 0.0022f, 0.0042f, 0.0076f, 0.0143f, 0.0232f, 0.0435f, 0.0776f, 0.1344f,
            0.2148f, 0.2839f, 0.3285f, 0.3483f, 0.3481f, 0.3362f, 0.3187f, 0.2908f, 0.2511f,
            0.1954f, 0.1421f, 0.0956f, 0.0580f, 0.0320f, 0.0147f, 0.0049f, 0.0024f, 0.0093f,
            0.0291f, 0.0633f, 0.1096f, 0.1655f, 0.2257f, 0.2904f, 0.3597f, 0.4334f, 0.5121f,
            0.5945f, 0.6784f, 0.7621f, 0.8425f, 0.9163f, 0.9786f, 1.0263f, 1.0567f, 1.0622f,
            1.0456f, 1.0026f, 0.9384f, 0.8544f, 0.7514f, 0.6424f, 0.5419f, 0.4479f, 0.3608f,
            0.2835f, 0.2187f, 0.1649f, 0.1212f, 0.0874f, 0.0636f, 0.0468f, 0.0329f, 0.0227f,
            0.0158f, 0.0114f, 0.0081f, 0.0058f, 0.0041f, 0.0029f, 0.0020f, 0.0014f, 0.0010f,
            0.0007f, 0.0005f, 0.0003f, 0.0002f, 0.0002f, 0.0001f, 0.0001f, 0.0001f, 0.0000f
        };
        const std::vector<float> kMatchingFunctionsY = {
            0.0000f, 0.0001f, 0.0001f, 0.0002f, 0.0004f, 0.0006f, 0.0012f, 0.0022f, 0.0040f,
            0.0073f, 0.0116f, 0.0168f, 0.0230f, 0.0298f, 0.0380f, 0.0480f, 0.0600f, 0.0739f,
            0.0910f, 0.1126f, 0.1390f, 0.1693f, 0.2080f, 0.2586f, 0.3230f, 0.4073f, 0.5030f,
            0.6082f, 0.7100f, 0.7932f, 0.8620f, 0.9149f, 0.9540f, 0.9803f, 0.9950f, 1.0000f,
            0.9950f, 0.9786f, 0.9520f, 0.9154f, 0.8700f, 0.8163f, 0.7570f, 0.6949f, 0.6310f,
            0.5668f, 0.5030f, 0.4412f, 0.3810f, 0.3210f, 0.2650f, 0.2170f, 0.1750f, 0.1382f,
            0.1070f, 0.0816f, 0.0610f, 0.0446f, 0.0320f, 0.0232f, 0.0170f, 0.0119f, 0.0082f,
            0.0057f, 0.0041f, 0.0029f, 0.0021f, 0.0015f, 0.0010f, 0.0007f, 0.0005f, 0.0004f,
            0.0002f, 0.0002f, 0.0001f, 0.0001f, 0.0001f, 0.0000f, 0.0000f, 0.0000f, 0.0000f
        };
        const std::vector<float> kMatchingFunctionsZ = {
            0.0065f, 0.0105f, 0.0201f, 0.0362f, 0.0679f, 0.1102f, 0.2074f, 0.3713f, 0.6456f,
            1.0391f, 1.3856f, 1.6230f, 1.7471f, 1.7826f, 1.7721f, 1.7441f, 1.6692f, 1.5281f,
            1.2876f, 1.0419f, 0.8130f, 0.6162f, 0.4652f, 0.3533f, 0.2720f, 0.2123f, 0.1582f,
            0.1117f, 0.0782f, 0.0573f, 0.0422f, 0.0298f, 0.0203f, 0.0134f, 0.0087f, 0.0057f,
            0.0039f, 0.0027f, 0.0021f, 0.0018f, 0.0017f, 0.0014f, 0.0011f, 0.0010f, 0.0008f,
            0.0006f, 0.0003f, 0.0002f, 0.0002f, 0.0001f, 0.0000f, 0.0000f, 0.0000f, 0.0000f,
            0.0000f, 0.0000f, 0.0000f, 0.0000f, 0.0000f, 0.0000f, 0.0000f, 0.0000f, 0.0000f,
            0.0000f, 0.0000f, 0.0000f, 0.0000f, 0.0000f, 0.0000f, 0.0000f, 0.0000f, 0.0000f,
            0.0000f, 0.0000f, 0.0000f, 0.0000f, 0.0000f, 0.0000f, 0.0000f, 0.0000f, 0.0000f
        };

        struct Rgb { float R, G, B; };

        Rgb ConvertToRgb(float cX, float cY, float cZ, float normalizedCy)
        {
            constexpr float xyzToSrgb[3][3] = {
                {3.2406f, -1.5372f, -0.4986f},
                {-0.9689f, 1.8758f, 0.0415f},
                {0.0557f, -0.2040f, 1.0570f}
            };
            float r = xyzToSrgb[0][0] * cX + xyzToSrgb[0][1] * cY + xyzToSrgb[0][2] * cZ;
            float g = xyzToSrgb[1][0] * cX + xyzToSrgb[1][1] * cY + xyzToSrgb[1][2] * cZ;
            float b = xyzToSrgb[2][0] * cX + xyzToSrgb[2][1] * cY + xyzToSrgb[2][2] * cZ;
            r = std::max(r, 0.0f) * normalizedCy;
            g = std::max(g, 0.0f) * normalizedCy;
            b = std::max(b, 0.0f) * normalizedCy;
            return {r, g, b};
        }

        Rgb GetBlackbodyColor(float temperature, float redshiftFactor)
        {
            float cX = 0.0f, cY = 0.0f, cZ = 0.0f;
            const float adjustedTemperature = std::max(1.0f, temperature) / std::max(1e-6f, redshiftFactor);

            constexpr int integrationNum = 81;
            for (int i = 0; i < integrationNum; ++i)
            {
                const float wavelength = 380.0f + static_cast<float>(i) * 5.0f;
                const float lambda = wavelength * 1e-9f;
                const float exponent = (kPlanckConstant * kLightSpeed) / (lambda * kBoltzmannConstant * adjustedTemperature);
                if (exponent > 100.0f) continue;

                const float intensity = ((2.0f * kPlanckConstant * std::pow(kLightSpeed, 2.0f)) / std::pow(lambda, 5.0f)) /
                    (std::exp(exponent) - 1.0f);

                cX += intensity * kMatchingFunctionsX[static_cast<std::size_t>(i)];
                cY += intensity * kMatchingFunctionsY[static_cast<std::size_t>(i)];
                cZ += intensity * kMatchingFunctionsZ[static_cast<std::size_t>(i)];
            }

            if (cY < 1e-12f) return {0.0f, 0.0f, 0.0f};

            const float maxXyz = std::max({cX, cY, cZ});
            float scale = 0.0f;
            if (maxXyz > 0.0f)
            {
                scale = 1.0f / maxXyz;
                cX *= scale;
                cY *= scale;
                cZ *= scale;
            }

            const float logCy = std::log(cY / scale);
            const float logMinCy = std::log(kMinCy);
            const float logMaxCy = std::log(kMaxCy);
            const float normalizedCy = std::clamp((logCy - logMinCy) / (logMaxCy - logMinCy), 0.0f, 1.0f);

            return ConvertToRgb(cX, cY, cZ, normalizedCy);
        }

        constexpr float kAccRMin = 0.01f;
        constexpr float kAccRMax = 50.0f;
        constexpr float kAccAngMomMin = 0.0f;
        constexpr float kAccAngMomMax = 100.0f;
        constexpr int kAccWidth = 512;
        constexpr int kAccHeight = 512;
        constexpr float kAccEpsilon = 0.01f;

        float CalculateAccelerationFactor(float angMomentumSqrd, float rSqrd)
        {
            const float r5 = std::pow(rSqrd, 2.5f);
            if (r5 < kAccEpsilon) return -1.5f * angMomentumSqrd / (1.0f * kAccEpsilon);
            return -2.0f * angMomentumSqrd / r5;
        }

        constexpr float kHrMassMin = 0.08f;
        constexpr float kHrMassMax = 100.0f;
        constexpr int kHrSize = 256;

        float MassToTemperature(float mass)
        {
            if (mass < 0.43f) return 2300.0f + (mass / 0.43f) * 700.0f;
            if (mass < 0.8f) return 3000.0f + ((mass - 0.43f) / 0.37f) * 1000.0f;
            if (mass < 1.0f) return 4000.0f + ((mass - 0.8f) / 0.2f) * 1500.0f;
            if (mass < 1.4f) return 5500.0f + ((mass - 1.0f) / 0.4f) * 1500.0f;
            if (mass < 2.1f) return 7000.0f + ((mass - 1.4f) / 0.7f) * 2000.0f;
            if (mass < 16.0f)
            {
                const float t = (mass - 2.1f) / 13.9f;
                return 9000.0f + t * 6000.0f;
            }
            const float t = std::min((mass - 16.0f) / (kHrMassMax - 16.0f), 1.0f);
            return 15000.0f + t * 30000.0f;
        }

        float MassToLuminosity(float mass)
        {
            if (mass < 0.43f) return std::pow(mass, 2.3f);
            if (mass < 2.0f) return std::pow(mass, 4.0f);
            if (mass < 20.0f) return std::pow(mass, 3.5f);
            return std::pow(mass, 3.0f);
        }

        float MassToRadius(float mass)
        {
            if (mass < 1.0f) return std::pow(mass, 0.8f);
            return std::pow(mass, 0.57f);
        }
    }

    std::shared_ptr<VulkanImage> GenerateBlackbodyLut(const std::shared_ptr<VulkanDevice>& device,
                                                       VulkanCommandPool& uploadPool, vk::Queue queue,
                                                       const std::shared_ptr<Logger>& logger)
    {
        std::vector<float> data;
        data.reserve(static_cast<std::size_t>(kBlackbodyWidth) * kBlackbodyHeight * 4);
        for (int y = 0; y < kBlackbodyHeight; ++y)
        {
            const float t = static_cast<float>(y) / static_cast<float>(kBlackbodyHeight - 1);
            const float redshift = kRedshiftMin + t * (kRedshiftMax - kRedshiftMin);
            for (int x = 0; x < kBlackbodyWidth; ++x)
            {
                const float s = static_cast<float>(x) / static_cast<float>(kBlackbodyWidth - 1);
                const float temperature = kTempMin + s * (kTempMax - kTempMin);
                const Rgb color = GetBlackbodyColor(temperature, redshift);
                data.push_back(color.R);
                data.push_back(color.G);
                data.push_back(color.B);
                data.push_back(1.0f);
            }
        }
        return UploadRgba32f(device, uploadPool, queue, kBlackbodyWidth, kBlackbodyHeight, data,
                             "BlackbodyLUT", logger);
    }

    std::shared_ptr<VulkanImage> GenerateAccelerationLut(const std::shared_ptr<VulkanDevice>& device,
                                                          VulkanCommandPool& uploadPool, vk::Queue queue,
                                                          const std::shared_ptr<Logger>& logger)
    {
        std::vector<float> data;
        data.reserve(static_cast<std::size_t>(kAccWidth) * kAccHeight * 4);
        const float logRMin = std::log(kAccRMin);
        const float logRMax = std::log(kAccRMax);
        for (int y = 0; y < kAccHeight; ++y)
        {
            const float t = static_cast<float>(y) / static_cast<float>(kAccHeight - 1);
            const float r = std::exp(logRMin + t * (logRMax - logRMin));
            const float rSqrd = r * r;
            for (int x = 0; x < kAccWidth; ++x)
            {
                const float s = static_cast<float>(x) / static_cast<float>(kAccWidth - 1);
                const float angMomentumSqrd = kAccAngMomMin + s * (kAccAngMomMax - kAccAngMomMin);
                const float factor = CalculateAccelerationFactor(angMomentumSqrd, rSqrd);
                data.push_back(factor);
                data.push_back(0.0f);
                data.push_back(0.0f);
                data.push_back(1.0f);
            }
        }
        return UploadRgba32f(device, uploadPool, queue, kAccWidth, kAccHeight, data,
                             "AccelerationLUT", logger);
    }

    std::shared_ptr<VulkanImage> GenerateHrDiagramLut(const std::shared_ptr<VulkanDevice>& device,
                                                       VulkanCommandPool& uploadPool, vk::Queue queue,
                                                       const std::shared_ptr<Logger>& logger)
    {
        std::vector<float> data;
        data.reserve(static_cast<std::size_t>(kHrSize) * 4);
        const float logMassMin = std::log(kHrMassMin);
        const float logMassMax = std::log(kHrMassMax);
        for (int i = 0; i < kHrSize; ++i)
        {
            const float t = static_cast<float>(i) / static_cast<float>(kHrSize - 1);
            const float mass = std::exp(logMassMin + t * (logMassMax - logMassMin));
            data.push_back(MassToTemperature(mass));
            data.push_back(MassToLuminosity(mass));
            data.push_back(MassToRadius(mass));
            data.push_back(1.0f);
        }
        return UploadRgba32f(device, uploadPool, queue, static_cast<std::uint32_t>(kHrSize), 1, data,
                             "HRDiagramLUT", logger);
    }

    std::shared_ptr<VulkanImage> LoadSkyboxTexture(const std::shared_ptr<VulkanDevice>& device,
                                                    VulkanCommandPool& uploadPool, vk::Queue queue,
                                                    const std::shared_ptr<IFileSystem>& fileSystem,
                                                    const std::filesystem::path& relativePath,
                                                    const std::shared_ptr<Logger>& logger)
    {
        const auto resolved = fileSystem->ResolvePath(relativePath);
        int width = 0, height = 0, channels = 0;
        float* pixels = stbi_loadf(resolved.string().c_str(), &width, &height, &channels, 4);
        if (!pixels)
        {
            if (logger) logger->Error("Failed to load skybox '{}': {}", resolved.string(),
                                      stbi_failure_reason() ? stbi_failure_reason() : "unknown error");
            std::vector<float> fallback(4, 0.0f);
            fallback[3] = 1.0f;
            return UploadRgba32f(device, uploadPool, queue, 1, 1, fallback, "SkyboxFallback", logger);
        }

        std::vector<float> data(pixels, pixels + static_cast<std::size_t>(width) * height * 4);
        stbi_image_free(pixels);

        return UploadRgba32f(device, uploadPool, queue, static_cast<std::uint32_t>(width),
                             static_cast<std::uint32_t>(height), data, "Skybox", logger);
    }
}
