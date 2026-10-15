MINEMU ?= minemu

FIXTURES := hello devices validation fault

.PHONY: all clean $(addprefix prepare-,$(FIXTURES))

all: $(addprefix prepare-,$(FIXTURES))

$(addprefix prepare-,$(FIXTURES)): prepare-%:
	$(MAKE) -C fixtures/$* MINEMU="$(MINEMU)" all

prepare-%:
	@:

clean:
	@for fixture in $(FIXTURES); do $(MAKE) -C fixtures/$$fixture clean; done
