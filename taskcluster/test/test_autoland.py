# Any copyright is dedicated to the public domain.
# http://creativecommons.org/publicdomain/zero/1.0/

import pytest
from mozunit import main

pytestmark = pytest.mark.slow
PARAMS = {
    "head_repository": "https://hg.mozilla.org/integration/autoland",
    "project": "autoland",
    # These ensure this isn't considered a backstop. The pushdate must
    # be slightly higher than the one in data/pushes.json, and
    # pushlog_id must not be a multiple of 10.
    "pushdate": 1593029536,
    "pushlog_id": "2",
}


def test_tasks_have_optimization(full_task_graph, filter_tasks):
    kinds = (
        "artifact_build",
        "build-fat-aar",
        "build",
        "generate-profile",
        "hazard",
        "instrumented-build",
        "spidermonkey",
        "static-analysis-autotest",
        "test",
        "valgrind",
    )
    errors = []
    for task in filter_tasks(
        full_task_graph, lambda t: t.kind in kinds and "ccov" not in t.label
    ):
        if not task.optimization:
            errors.append(task.label)

    if errors:
        label_str = "\n  ".join(errors)
        s_are = " is" if len(errors) == 1 else "s are"
        pytest.fail(
            f"The following task{s_are} missing an optimization:\n  {label_str}"
        )


def test_generate_graph(optimized_task_graph):
    """Simply tests that generating the graph does not fail."""
    assert len(optimized_task_graph.tasks) > 0


@pytest.mark.parametrize(
    "func",
    (
        pytest.param(
            lambda t: t.kind == "build" and "fuzzing" in t.attributes["build_platform"],
            id="no fuzzing builds",
        ),
        pytest.param(
            lambda t: t.kind == "build" and "ccov" in t.attributes["build_platform"],
            id="no ccov builds",
        ),
    ),
)
def test_tasks_are_not_scheduled(
    optimized_task_graph, filter_tasks, print_dependents, func
):
    """Ensure the specified tasks are not scheduled on autoland."""
    tasks = [t.label for t in filter_tasks(optimized_task_graph, func)]
    for t in tasks:
        print_dependents(optimized_task_graph, t)
    assert tasks == []


if __name__ == "__main__":
    main()
