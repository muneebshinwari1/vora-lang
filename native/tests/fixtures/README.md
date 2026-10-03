# Synthetic state compatibility fixtures

checkpoint-v1.json fixes the supported full 0.4 checkpoint schema. Its only
relocated field is __TEST_WORKSPACE__, substituted by the isolated test directory.
The completed result must restore without new calls. Removing the timeout field
represents an intentionally unsupported earlier-preview checkpoint.

memory-v1.json was generated with the older local 0.4.0-dev staged binary using
archived.vora and the dummy input baseline. The release candidate must consume
that history and append the next task. No real user state or secrets are stored.
