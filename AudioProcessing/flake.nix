{
  description = "Daisy Seed development environment";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/22.05"; # older, stable for gcc-arm-embedded
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = { self, nixpkgs, flake-utils }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = import nixpkgs {
          inherit system;
        };

        # Use older ARM GCC (important!)
        armToolchain = pkgs.gcc-arm-embedded;

      in {
        devShells.default = pkgs.mkShell {
          buildInputs = [
            armToolchain
            pkgs.dfu-util
            pkgs.openocd
            pkgs.cmake
            pkgs.gnumake
            pkgs.git
            pkgs.python3
          ];

          shellHook = ''
            echo "Daisy Seed dev environment ready"
            echo "arm-none-eabi-gcc version:"
            arm-none-eabi-gcc --version
          '';
        };
      });
}