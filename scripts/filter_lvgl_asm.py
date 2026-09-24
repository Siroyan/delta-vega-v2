Import("env")


def skip_arm_assembly(env, node):
    return None


# LVGL 9.2.2 ships ARM Helium/NEON assembly that PlatformIO otherwise tries
# to link into the RISC-V firmware, even though these backends are disabled.
env.AddBuildMiddleware(
    skip_arm_assembly, "*lvgl/src/draw/sw/blend/*/*.S"
)
