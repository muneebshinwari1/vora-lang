"""Equivalent graph via the same runtime; not an external-framework benchmark."""
from vora.model import Agent, Step, Workflow
from vora.providers import DemoProvider
from vora.runtime import run

workflow = Workflow(
    name="CompanyResearch", input_name="company",
    agents={
        "researcher": Agent("researcher", "Summarize what is known. Label uncertainty; you have no web tool."),
        "critic": Agent("critic", "Identify risks and assumptions without inventing evidence."),
        "writer": Agent("writer", "Write a concise proposal from the supplied analysis."),
    },
    steps=(
        Step("facts", "researcher", "Analyze this company: {company}", ()),
        Step("risks", "critic", "Assess this company: {company}", ()),
        Step("report", "writer", "Facts: {facts}\nRisks: {risks}", ("facts", "risks")),
    ),
    output="report",
)

if __name__ == "__main__":
    print(run(workflow, "An example software company", DemoProvider()).result)
