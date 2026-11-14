#!/bin/bash
# The PC tools of the Vulkan renderer (port/docs/vulkan-bauplan.md) without root: downloads the Ubuntu packages
# libvulkan-dev (headers), vulkan-validationlayers (the Khronos validation layer), spirv-tools (spirv-val ...),
# glslang-tools (glslangValidator) and vulkan-tools (vulkaninfo) with apt-get download and unpacks them into
# ~/vk-tools/root; the system stays as it is. Mesa's lavapipe (mesa-vulkan-drivers) must be installed already
# (port/tools/whole_setup_linux.sh). Then, in every shell that runs the checks:
#
#   . ~/vk-tools/env.sh        (PATH, LD_LIBRARY_PATH, VK_LAYER_PATH, VK_ICD_FILENAMES = lavapipe)
#
# With root, `sudo port/tools/whole_setup_linux.sh` installs the same packages for good instead.
set -euo pipefail
dest="${VK_TOOLS_HOME:-$HOME/vk-tools}"
mkdir -p "$dest/debs"
cd "$dest/debs"
apt-get download libvulkan-dev vulkan-validationlayers spirv-tools glslang-tools vulkan-tools
rm -rf "$dest/root"
for deb in "$dest"/debs/*.deb; do
  dpkg -x "$deb" "$dest/root"
done
cat > "$dest/env.sh" << 'ENV'
# source me: the Vulkan tools of port/tools/vk_tools_linux.sh (no system change)
export VK_TOOLS="${VK_TOOLS_HOME:-$HOME/vk-tools}/root"
export PATH="$VK_TOOLS/usr/bin:$PATH"
export LD_LIBRARY_PATH="$VK_TOOLS/usr/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export VK_LAYER_PATH="$VK_TOOLS/usr/share/vulkan/explicit_layer.d"
# Mesa's lavapipe: the same rasterizer as llvmpipe, which draws the GLES pictures of the PC checks
export VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json
ENV
. "$dest/env.sh"
echo "Vulkan tools in $dest/root:"
vulkaninfo --summary 2> /dev/null | grep -E "deviceName|apiVersion|driverInfo" | sed 's/^/  /' || echo "  (vulkaninfo: no device; is mesa-vulkan-drivers installed?)"
command -v spirv-val glslangValidator | sed 's/^/  /'
echo "Next: . $dest/env.sh"
