// Batch m15: the default callbacks of the job controller returned garbage, so the support tree
// aborted before its first step.
//
// MSVC mangles the lambdas of a default member initializer and of a default function argument
// with a counter that is per namespace, not per class. The same mangled name can therefore stand
// for different lambdas in different translation units, and the linker keeps one of the bodies: a
// void `[]() {}` replaced the body of `[]() { return false; }`. A default
// `Slic3r::sla::JobController` then answered true to stopcondition() with 128 or 160 in the return
// register, so DefaultSupportTree::execute jumped to ABORT and no head, pillar or base was built,
// and `Slic3r::sla::RotOptimizeParams` could stop the optimiser the same way. Which body the linker
// picked depended on what other tests had left behind, so the failures moved with the test order.
//
// Every one of these defaults is a named function now, so there is nothing left for the linker to
// merge. What is checked here is that a default constructed controller really answers false, does
// not throw when it is cancelled and can be called, and that the types built on top of it - the
// support tree builder, the rotation optimizer parameters and the optimizer stop criteria - keep
// the same defaults.
#include <catch2/catch_test_macros.hpp>

#include <string>

#include "Slic3r/Biz/Algorithms/Optimize/Optimizer.hpp"
#include "libslic3r/SLA/JobController.hpp"
#include "libslic3r/SLA/Rotfinder.hpp"
#include "libslic3r/SLA/SupportTreeBuilder.hpp"

TEST_CASE("JobController: a default job controller never stops")
{
    Slic3r::sla::JobController ctl;

    CHECK(static_cast<bool>(ctl.stopcondition));
    CHECK_FALSE(ctl.stopcondition());
}

TEST_CASE("JobController: the default cancel callback returns instead of throwing")
{
    Slic3r::sla::JobController ctl;

    CHECK(static_cast<bool>(ctl.cancelfn));
    CHECK_NOTHROW(ctl.cancelfn());
}

TEST_CASE("JobController: the default status callback can be called")
{
    Slic3r::sla::JobController ctl;

    CHECK(static_cast<bool>(ctl.statuscb));
    CHECK_NOTHROW(ctl.statuscb(0, std::string{"job controller"}));
}

TEST_CASE("SupportTreeBuilder: a default tree builder never stops")
{
    Slic3r::sla::SupportTreeBuilder builder;

    CHECK_FALSE(builder.ctl().stopcondition());
}

TEST_CASE("RotOptimizeParams: the default status callback lets the optimisation go on")
{
    Slic3r::sla::RotOptimizeParams params;

    CHECK(params.statuscb()(0));
    CHECK(params.statuscb()(100));
}

TEST_CASE("Optimize StopCriteria: the default stop condition never stops")
{
    Slic3r::Biz::Algorithms::Optimize::StopCriteria criteria;

    CHECK_FALSE(criteria.stop_condition());
}