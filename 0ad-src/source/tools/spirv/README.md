# Instructions

- Install Python 3 and the Python dependencies

    ```sh
    pip install -r requirements.txt
    ```

- Install `glslc` and spirv-tools 2023+ (the easiest way is to install Vulkan SDK)
- For improved performance you may also install [libyaml](https://github.com/yaml/libyaml)

- Run the compile.py script

    ```sh
    python compile.py path-to-folder-with-input-mods mod-output-path rules-path
    ```
