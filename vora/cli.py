import argparse
import json
import sys
from pathlib import Path

from .parser import ParseError, load, plan


def main(argv=None):
    parser = argparse.ArgumentParser(description="Vora experimental agent workflow language")
    sub = parser.add_subparsers(dest="command", required=True)
    for command in ("check", "plan", "run"):
        cmd = sub.add_parser(command)
        cmd.add_argument("file", type=Path)
        if command == "plan":
            cmd.add_argument("--mermaid", action="store_true")
        if command == "run":
            cmd.add_argument("--input", required=True)
            cmd.add_argument("--provider", choices=("demo", "ollama"), default="demo")
            cmd.add_argument("--model")
            cmd.add_argument("--workers", type=int, default=4)
            cmd.add_argument("--retries", type=int, default=0)
            cmd.add_argument("--max-calls", type=int, default=20)
            cmd.add_argument("--trace", type=Path)
    args = parser.parse_args(argv)
    try:
        workflow = load(args.file)
        if args.command == "check":
            print(f"Valid: {workflow.name}, {len(workflow.steps)} steps")
            return 0
        if args.command == "plan":
            if args.mermaid:
                print("flowchart TD")
                ids = {step.name: f"s{index}" for index, step in enumerate(workflow.steps)}
                for step in workflow.steps:
                    print(f'  {ids[step.name]}["{step.name} / {step.agent}"]')
                    for dep in step.dependencies:
                        print(f"  {ids[dep]} --> {ids[step.name]}")
            else:
                print(json.dumps(plan(workflow), indent=2))
            return 0
        from .providers import DemoProvider, OllamaProvider
        from .runtime import ExecutionError, run
        if args.provider == "ollama":
            if not args.model:
                parser.error("--model is required with --provider ollama")
            provider = OllamaProvider(model=args.model)
        else:
            print("DEMO: deterministic simulated agents; no model or web requests.", file=sys.stderr)
            provider = DemoProvider()
        try:
            result = run(workflow, args.input, provider, max_workers=args.workers,
                         retries=args.retries, max_calls=args.max_calls)
        except ExecutionError as exc:
            if args.trace:
                args.trace.write_text(json.dumps(exc.events, indent=2), encoding="utf-8")
            print(f"Execution failed: {exc}", file=sys.stderr)
            return 1
        if args.trace:
            args.trace.write_text(json.dumps(result.events, indent=2), encoding="utf-8")
        print(result.result)
        return 0
    except (ParseError, OSError, ValueError) as exc:
        print(f"Error: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
