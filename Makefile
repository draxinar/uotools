SUBDIRS = wombat

all: format-check
	@for d in $(SUBDIRS); do $(MAKE) -C $$d || exit; done

clean:
	@for d in $(SUBDIRS); do $(MAKE) -C $$d clean; done

test:
	@for d in $(SUBDIRS); do echo "=== $$d ==="; $(MAKE) -C $$d test || exit; done

format:
	find . \( -name "*.c" -o -name "*.h" \) -exec clang-format -i {} \;

# Fail (exit non-zero) if any source file would be modified by `make format`.
# Used by CI to enforce style; clang-format >= 10 is required for --dry-run.
format-check:
	find . \( -name "*.c" -o -name "*.h" \) -exec clang-format --dry-run --Werror {} +

.PHONY: all clean test format format-check
