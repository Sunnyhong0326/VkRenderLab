#include "Application.hpp"

#include <charconv>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string_view>

int main(int argc, char** argv)
{
    try
    {
        RunOptions options;
        for (int i = 1; i < argc; ++i)
        {
            const std::string_view argument = argv[i];
            if (argument == "--help")
            {
                std::cout << "Usage: vk_render_lab [--validate] [--frames count] [scene.gltf | scene.glb]\n";
                return 0;
            }
            if (argument == "--validate")
                options.validate = true;
            else if (argument == "--frames")
            {
                if (++i == argc)
                    throw std::runtime_error("--frames requires a positive integer.");
                const std::string_view count = argv[i];
                const auto end = count.data() + count.size();
                const auto result = std::from_chars(count.data(), end, options.frameLimit);
                if (result.ec != std::errc{} || result.ptr != end || options.frameLimit == 0)
                    throw std::runtime_error("--frames requires a positive integer.");
            }
            else if (!argument.starts_with('-') && options.scene.empty())
                options.scene = argv[i];
            else
                throw std::runtime_error("Unexpected argument: " + std::string(argument));
        }
        Application application;
        application.run(options);
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
