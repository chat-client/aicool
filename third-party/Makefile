all:
	@(cd acl; make all_lib ENV_CC="clang -arch arm64 -arch x86_64" ENV_CPP="clang++ -arch arm64 -arch x86_64")
	@(cd sqlite; make MACOS_UNIVERSAL=1)

clean:
	@(cd acl; make clean)
	@(cd sqlite; make clean)
