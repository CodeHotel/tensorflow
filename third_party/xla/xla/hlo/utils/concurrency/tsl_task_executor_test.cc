/* Copyright 2025 The OpenXLA Authors.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
==============================================================================*/

#include "xla/hlo/utils/concurrency/tsl_task_executor.h"

#include <utility>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include "absl/status/status.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"

namespace xla::concurrency {
namespace {

using ::testing::ElementsAreArray;
using ::testing::status::StatusIs;

TEST(TslTaskExecutorTest, ParallelismOneExecutesInOrder) {
  const int kSlowWrite = 42;
  const int kMediumWrite = 79;
  const int kFastWrite = 255;

  const unsigned int kSlowWait = 1000;
  const unsigned int kMediumWait = 300;
  const unsigned int kFastWait = 10;

  auto task_executor = TslTaskExecutor(3);

  std::vector<int> results;

  std::vector<Task> actions;
  actions.push_back([&results, kSlowWrite]() {
    absl::SleepFor(absl::Milliseconds(kSlowWait));
    results.push_back(kSlowWrite);
    return absl::OkStatus();
  });
  actions.push_back([&results, kMediumWrite]() {
    absl::SleepFor(absl::Milliseconds(kMediumWait));
    results.push_back(kMediumWrite);
    return absl::OkStatus();
  });
  actions.push_back([&results, kFastWrite]() {
    absl::SleepFor(absl::Milliseconds(kFastWait));
    results.push_back(kFastWrite);
    return absl::OkStatus();
  });

  ASSERT_OK(task_executor.ExecuteIndependentTasks(std::move(actions), 1));
  EXPECT_THAT(results,
              ElementsAreArray({kSlowWrite, kMediumWrite, kFastWrite}));
}

TEST(TslTaskExecutorTest, SuccessfulExecutionReturnsOkStatus) {
  auto task_executor = TslTaskExecutor(3);

  std::vector<int> results;

  std::vector<Task> actions;
  for (int i = 0; i < 20; ++i) {
    actions.push_back([]() { return absl::OkStatus(); });
  }

  EXPECT_OK(task_executor.ExecuteIndependentTasks(std::move(actions)));
}

TEST(TaskExecutor, OnFailureNotAllWorkFinishes) {
  const int kBeforeCount = 20;
  const int kAfterCount = 100;
  const int kThreadCount = 5;
  auto task_executor = TslTaskExecutor(kThreadCount);

  int finish_counter = 0;
  absl::Mutex mu_finish_counter;

  std::vector<Task> actions;
  for (int i = 0; i < kBeforeCount; ++i) {
    actions.push_back([&]() {
      absl::MutexLock lock{&mu_finish_counter};
      ++finish_counter;
      absl::SleepFor(absl::Milliseconds(10));
      return absl::OkStatus();
    });
  }

  actions.push_back(
      []() { return absl::UnimplementedError("force a failure"); });

  for (int i = 0; i < kAfterCount; ++i) {
    actions.push_back([&]() {
      absl::MutexLock lock{&mu_finish_counter};
      ++finish_counter;
      absl::SleepFor(absl::Milliseconds(10));
      return absl::OkStatus();
    });
  }

  ASSERT_THAT(task_executor.ExecuteIndependentTasks(std::move(actions)),
              StatusIs(absl::StatusCode::kUnimplemented));
}

}  // namespace
}  // namespace xla::concurrency
