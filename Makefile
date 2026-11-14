# ./rae runs the Rae this repository pins (RAE_ROOT=<a Rae checkout> to use another).
.DEFAULT_GOAL := help

.PHONY: help test format clean install-hooks banned-terms-gate run run-player

help:
	@echo "make run-player FILE=<video>  play a video file in the simple player (no FILE: pick one)"
	@echo "make run FILE=<video>         the same"
	@echo "make test                     run the tests (tests/, the player's headless test, the checks)"
	@echo "make format                   format the Rae sources"
	@echo "make install-hooks            install the git hooks (banned-terms gate, Git LFS)"
	@echo "make banned-terms-gate        check the tree for banned terms"
	@echo "make clean                    remove build output"

test: banned-terms-gate
	sh tools/test.sh

# Fails if a term from the list outside the repository (~/.config/rae/banned-terms.txt,
# the same as Rae's) is in a tracked file or an unpushed commit message.
banned-terms-gate:
	@bash tools/banned-terms-gate.sh

# Git does not version hooks: link tools/git-hooks/ into .git/hooks (the banned-terms gate
# before a commit and on its message, and Git LFS).
install-hooks:
	@for hook in pre-commit commit-msg post-commit post-merge post-checkout pre-push; do \
	  ln -sfn "$(CURDIR)/tools/git-hooks/$$hook" .git/hooks/$$hook; \
	done
	@echo "Installed the hooks of tools/git-hooks/"

# The simple video player (examples/simplePlayer): `make run-player FILE=clip.mp4`, or
# without FILE it asks for a file. `make run` is the same.
run: run-player

run-player:
	./rae run examples/simplePlayer/Main.rae $(if $(FILE),-- "$(FILE)")

format:
	@[ ! -d packages ] || ./rae format packages
	@[ ! -d tests ] || ./rae format tests
	@[ ! -d examples ] || ./rae format examples

clean:
	find . -name .rae -type d -prune -exec rm -rf {} +
	find . -name build -type d -prune -exec rm -rf {} +
