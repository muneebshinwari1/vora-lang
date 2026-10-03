class Vora < Formula
  desc "Native language for bounded local AI agent workflows"
  homepage "https://github.com/muneebshinwari1/vora-lang"
  url "https://github.com/muneebshinwari1/vora-lang/archive/refs/tags/v0.4.0.tar.gz"
  version "0.4.0"
  sha256 "7130c0d74d8a3d5409ccba16a2e3453676f336dea356b086242d85598699bf86"
  license "MIT"

  depends_on "cmake" => :build
  depends_on "curl"

  def install
    curl = Formula["curl"]
    extension = OS.mac? ? "dylib" : "so"
    system "cmake", "-S", "native", "-B", "build", *std_cmake_args,
           "-DCURL_INCLUDE_DIR=#{curl.opt_include}",
           "-DCURL_LIBRARY=#{curl.opt_lib}/libcurl.#{extension}"
    system "cmake", "--build", "build", "--parallel", "2"
    system "ctest", "--test-dir", "build", "--output-on-failure"
    system "cmake", "--install", "build"
  end

  test do
    assert_match "Vora 0.4.0 -", shell_output("#{bin}/vora --help")
    assert_match "Valid", shell_output("#{bin}/vora check #{pkgshare}/examples/quickstart-fast.vora")
    (testpath/"stats.vora").write <<~EOS
      workflow Stats(input):
          tool stats = "text_stats"
          result = stats({"text": "{input}"})
          return result
    EOS
    assert_match '"words":2', shell_output("#{bin}/vora run stats.vora --input 'hello world' --allow-tools text_stats")
  end
end
