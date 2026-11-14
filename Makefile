# ./rae runs the Rae this repository pins (RAE_ROOT=<a Rae checkout> to use another).
.DEFAULT_GOAL := test

.PHONY: test format clean

test:
	sh tools/test.sh

format:
	@[ ! -d packages ] || ./rae format packages
	@[ ! -d tests ] || ./rae format tests

clean:
	find . -name .rae -type d -prune -exec rm -rf {} +
	find . -name build -type d -prune -exec rm -rf {} +
