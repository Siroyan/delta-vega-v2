Import("env")
from os.path import dirname, join

# Course geometry and artwork stay beside their source files under assets/.
lvgl_dir = join(env.subst("$PROJECT_LIBDEPS_DIR"), env.subst("$PIOENV"), "lvgl")
env.Append(CPPPATH=[lvgl_dir, dirname(lvgl_dir),
                    join(env.subst("$PROJECT_DIR"), "lib", "vega_core", "src")])
env.BuildSources("$BUILD_DIR/course_assets", "$PROJECT_DIR/assets",
                 src_filter="+<**/*.cpp>")
