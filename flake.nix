{
  description = "DoomBF";

  inputs = {
    nixpkgs.url = "github:nixos/nixpkgs?ref=nixos-unstable";
  };

  outputs = { self, nixpkgs }: let
    system = "x86_64-linux";
    pkgs = import nixpkgs { inherit system; };
    # Target the embedded riscv32 profile
    crossPkgs = import nixpkgs {
      inherit system;
      crossSystem = pkgs.lib.systems.examples.riscv32-embedded // {
        # Forces standard 32-bit integer base with soft float
        gcc.arch = "rv32i";
        gcc.abi = "ilp32";
      };
    };
    mkShellWith = pkg: pkgs.mkShell {
      packages = [
        pkg
        pkgs.gcc
        pkgs.gnumake
        pkgs.libX11.dev
        pkgs.libX11
        pkgs.lightning
        (pkgs.python3.withPackages (p: with p; [
          pyelftools
          capstone
        ]))
      ];
    };
    riscv64-binutils-bin = pkgs.stdenvNoCC.mkDerivation {
      name = "binutils-riscv64-unknown-elf-debian";
      nativeBuildInputs = [ pkgs.autoPatchelfHook ];
      buildInputs = with pkgs; [
        zlib
        zstd
      ];
      src = pkgs.fetchurl {
        url = "http://ftp.debian.org/debian/pool/main/b/binutils-embedded/binutils-riscv64-unknown-elf_2.46-1+28_amd64.deb";
        hash = "sha256-9h3nk+ciTqqrWRIqDdXWV2X9yekMZAkLumYISrhgrec=";
      };
      unpackPhase = ''
        ${pkgs.dpkg}/bin/dpkg -x $src .
      '';
      installPhase = ''
        cp -r usr $out
      '';
    };
    riscv64-gcc-bin = pkgs.stdenvNoCC.mkDerivation {
      name = "gcc-riscv64-unknown-elf-debian";
      nativeBuildInputs = with pkgs; [
        autoPatchelfHook
        makeWrapper
      ];
      buildInputs = with pkgs; [
        glib
        ncurses
        python312
        zlib
        zstd
        libmpc
        mpfr
        gmp
        expat
        isl_0_23
      ];
      propagatedBuildInputs = [
        riscv64-binutils-bin
      ];
      src = pkgs.fetchurl {
        urls = [
          "http://ftp.debian.org/debian/pool/main/g/gcc-riscv64-unknown-elf/gcc-riscv64-unknown-elf_15.3.0-24_amd64.deb"
          "https://archive.org/download/gcc-riscv64-unknown-elf_15.3.0-24_amd64/gcc-riscv64-unknown-elf_15.3.0-24_amd64.deb"
        ];
        hash = "sha256-BFJ/oiXrtgbDunmw0Sx0yGUpvvpK2i4MHU6MYWe2nhw=";
      };
      unpackPhase = ''
        ${pkgs.dpkg}/bin/dpkg -x $src .
      '';
      installPhase = ''
        cp -r usr $out
        ln -s ${riscv64-binutils-bin}/bin/riscv64-unknown-elf-as $out/libexec/gcc/riscv64-unknown-elf/15/as
      '';
    };
  in {
    devShells.${system} = {
      default = mkShellWith riscv64-gcc-bin;
      src = mkShellWith crossPkgs.stdenv.cc;
    };
  };
}
