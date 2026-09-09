# Third-party components

- JSON for Modern C++: nlohmann/json **v3.12.0**, MIT license, vendored as `native/vendor/nlohmann/json.hpp`. Source: https://raw.githubusercontent.com/nlohmann/json/v3.12.0/single_include/nlohmann/json.hpp . Header SHA256: `AAF127C04CB31C406E5B04A63F1AE89369FCCDE6D8FA7CDDA1ED4F32DFC5DE63`. License text is in the same folder.
- llama.cpp: official Windows CPU release **b10859**. Pinned upstream URL, release digest and local verification are in `native/local-model/manifest.json`; license retained with local model artifacts.
- Qwen3-0.6B-GGUF: official Qwen Q8_0 model, revision **23749fefcc72300e3a2ad315e1317431b06b590a**. Exact source URL, upstream SHA256 and retained license are documented under `native/local-model/` and [model setup](model-setup.md).

No hosted API credentials or third-party paid services were used for the verified local run.
