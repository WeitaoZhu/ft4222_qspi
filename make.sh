FT4222_QSPI_TOOL="ft4222-qspi"

rm -rf version.h

if [[ $(git diff --stat) != '' ]]; then
	git_commit_dot=g$(git rev-parse --short HEAD)-dirty
else
	git_commit_dot=g$(git rev-parse --short HEAD)
fi

git_tag_info=$(git describe --tags --abbrev=0)

echo "#define FT4222_QSPI_TOOL_GIT_COMMIT \"$git_commit_dot \"" > version.h
echo "#define FT4222_QSPI_TOOL_GIT_TAG \"$git_tag_info\"" >> version.h

#cc ft4222_tool.c -lft4222 -Wl,-rpath,/usr/local/lib -o $FT4222_QSPI_TOOL

cc -static ft4222_tool.c -lft4222 -Wl,-rpath,/usr/local/lib -ldl -lpthread -lrt -lstdc++ -o $FT4222_QSPI_TOOL
