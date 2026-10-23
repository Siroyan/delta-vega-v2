#include "strategy_motion.h"

#include <cassert>
#include <cmath>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <string>
#include <vector>

using namespace sim;
static void near(double actual, double expected) { assert(std::abs(actual - expected) < 1e-6); }
static void collect(const MotionEvent &event, void *context) {
  static_cast<std::vector<MotionEvent> *>(context)->push_back(event);
}

int main(int argc, char **argv) {
  if (argc == 8) {
    std::ifstream file(argv[1], std::ios::binary);
    std::string bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    CourseSpec course{argv[2], static_cast<uint8_t>(std::stoi(argv[3])),
                      std::stod(argv[4]), std::stod(argv[5]), std::stod(argv[6])};
    DrivingStrategy strategy;
    std::string error;
    const bool accepted = parseDrivingStrategy(bytes.data(), bytes.size(), course, strategy, error);
    if (std::string(argv[7]) == "reject") { assert(!accepted); return 0; }
    if (!accepted) std::cerr << error << '\n';
    assert(accepted);
    assert(strategy.laps.size() == course.lap_count);
    Motion model;
    model.reset(course, {30, 0, 0.3, 0.03}, &strategy);
    for (int i = 0; i < 360000 && !model.state().finished && !model.state().stalled; ++i)
      model.advance(0.1);
    std::cout << strategy.plan_id << " distance=" << model.state().total_m
              << " elapsed=" << model.state().elapsed_s << " finished=" << model.state().finished
              << " stalled=" << model.state().stalled << '\n';
    return 0;
  }
  CourseSpec course{"course-v1", 2, 100, 100, 100};
  const MotionConfig config{36, 0, 1, 0.1};
  assert(validMotionConfig(config));
  assert(!validMotionConfig({36, 40, 1, 0.1}));
  assert(!validMotionConfig({36, 0, 0, 0.1}));
  assert(!validMotionConfig({36, 0, 1, std::numeric_limits<double>::quiet_NaN()}));
  DrivingStrategy strategy{"test", 400, {{200, {{0, 20}, {60, 75}}},
                                           {200, {{0, 20}, {60, 75}}}}};
  Motion model;
  model.reset(course, config, &strategy);
  model.advance(2);
  near(model.state().speed_mps, 2);
  near(model.state().total_m, 2);
  model.advance(2);
  near(model.state().speed_mps, 4);
  near(model.state().total_m, 8);
  model.advance(std::sqrt(40.0) - 4 + 1);
  near(model.state().speed_mps, std::sqrt(40.0) - 0.1);
  assert(!model.state().engine_on);

  Motion large, small;
  large.reset(course, config, &strategy);
  small.reset(course, config, &strategy);
  std::vector<MotionEvent> large_events, small_events;
  large.advance(1000, collect, &large_events);
  for (int i = 0; i < 10000 && !small.state().finished; ++i) small.advance(0.007, collect, &small_events);
  assert(large.state().finished && small.state().finished);
  near(large.state().total_m, 200);
  near(large.state().elapsed_s, small.state().elapsed_s);
  assert(large_events.size() == small_events.size());
  assert(large_events.size() == 10);
  for (size_t i = 0; i < large_events.size(); ++i) {
    assert(large_events[i].kind == small_events[i].kind);
    near(large_events[i].s_m, small_events[i].s_m);
    near(large_events[i].elapsed_s, small_events[i].elapsed_s);
  }
  // Targets remain comparison values and do not force the simulated finish time.
  assert(std::abs(large.state().elapsed_s - strategy.total_target_s) > 100);

  Motion constant;
  constant.reset(course, config);
  constant.advance(25);
  assert(constant.state().finished);
  near(constant.state().elapsed_s, 20);
  near(constant.state().total_m, 200);
  DrivingStrategy stall{"stall", 400, {{200, {{0, 1}, {20, 21}}}, {200, {{0, 1}}}}};
  model.reset(course, {36, 0, 1, 1}, &stall);
  model.advance(100);
  assert(model.state().stalled && !model.state().finished);
  near(model.state().total_m, 2);
  near(model.state().speed_mps, 0);
  near(model.state().elapsed_s, 2 * std::sqrt(2.0));
  model.advance(100);
  near(model.state().total_m, 2);

  // The speed cap and long ON intervals never overshoot the configured speed.
  DrivingStrategy full{"full", 400, {{200, {{0, 100}}}, {200, {{0, 100}}}}};
  model.reset(course, {3.6, 0, 1, 0.1}, &full);
  model.advance(50);
  near(model.state().speed_mps, 1);
  near(model.state().total_m, 49.5);
  model.advance(-1);
  model.advance(std::numeric_limits<double>::quiet_NaN());
  near(model.state().total_m, 49.5);
  std::cout << "PASS: analytic acceleration/coasting, speed cap, boundaries, targets, stall, constant mode\n";
}
