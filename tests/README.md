# Test data

Shared test files that both the C++ engine and the Python front end use:

- `target_examples.json`: valid and invalid targets for the target format (task 2.1)
- `parser_samples.json`: sample requests and the target each should produce (task 9.1)
- `optimizer_cases.json`: hand-calculated optimizer test cases (task 8.1). The rules
  they follow are in [`docs/optimizer-rules.md`](../docs/optimizer-rules.md);
  `frontend/tests/test_optimizer_cases.py` re-adds every expected order from
  the chain files to check the math and catch price changes

Code tests live with their code, in `engine/tests/` and `frontend/tests/`.
