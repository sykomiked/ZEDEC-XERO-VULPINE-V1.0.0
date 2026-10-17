# The ZXV OS node CLI formula. The release tag publishes the statically
# linked daemon; the formula verifies the tarball's sha256.
class Zxv < Formula
  desc "ZXV OS node: the C11 daemon, 36n9/9n63 planes, seed bank resolution"
  homepage "https://zedec.s3.filebase.io"
  url "https://github.com/36N9-GENETICS/zxv/releases/download/v1.0.0/zxv-v1.0.0.tar.gz"
  sha256 "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"
  license "Apache-2.0"

  def install
    bin.install "zxv"
    bin.install "36n9"
    bin.install "9n63"
  end

  test do
    system "#{bin}/zxv", "--version"
    system "#{bin}/36n9", "--ai-manifest"
  end
end
