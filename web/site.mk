# Website build for GitHub Pages: the show as WebAssembly, the flipbooks, the whitepaper as HTML
# and PDF, in English and German. Needs emcc (Emscripten), pandoc, typst, python3 and node on
# top of the C toolchain. `make` on the Pi never touches any of this. Output: build/site/.
SITE   := build/site
EMCC   ?= emcc
PANDOC ?= pandoc
# Resolved through the shell: make's own PATH search trips over emsdk's node/ directory.
NODE   ?= $(shell command -v node)
BASE   := https://bmmmm.github.io/tvtv

# build/visuals.c is the generated table of uploaded visuals (empty here), see the Makefile. The
# social channels are not on the website, but their scenes link against the snapshot reader and
# the dem0 VM.
WEB_SRCS    := src/core.c src/scenes.c src/social.c src/dem0.c $(wildcard src/scenes/*.c) build/visuals.c web/shim.c
WEB_EXPORTS := _web_init,_web_w,_web_h,_web_scene_count,_web_loop_count,_web_scene_name,_web_scene_dur,_web_start,_web_current,_web_time,_web_length,_web_step
PAGES := index.html index.de.html whitepaper.html whitepaper.de.html flipbook.html flipbook.de.html
SOCIAL_AVATARS := $(addprefix $(SITE)/img/,social-roehre.png social-einwurf.png social-videotext.png)
SITE_FILES := $(addprefix $(SITE)/,$(PAGES) 404.html whitepaper.en.pdf whitepaper.de.pdf bc101.js \
              player.js style.css favicon.svg robots.txt sitemap.xml img/.stamp) $(SOCIAL_AVATARS)

site: $(SITE_FILES)

# Markdown page -> HTML through the one template. $1 = source, $2 = target.
page = $(PANDOC) $(1) -f markdown-smart -t html5 --syntax-highlighting=none --template web/template.html -o $(2)

$(SITE)/index.html: web/index.en.md web/template.html web/site.mk | $(SITE)
	$(call page,$<,$@)
$(SITE)/index.de.html: web/index.de.md web/template.html web/site.mk | $(SITE)
	$(call page,$<,$@)
$(SITE)/404.html: web/404.md web/template.html web/site.mk | $(SITE)
	$(call page,$<,$@)
$(SITE)/whitepaper.html: docs/whitepaper.en.md web/template.html web/site.mk $(SITE)/img/.stamp $(SOCIAL_AVATARS) | $(SITE)
	$(call page,$<,$@)
$(SITE)/whitepaper.de.html: docs/whitepaper.de.md web/template.html web/site.mk $(SITE)/img/.stamp $(SOCIAL_AVATARS) | $(SITE)
	$(call page,$<,$@)
$(SITE)/whitepaper.en.pdf: docs/whitepaper.en.md web/site.mk $(SITE)/img/.stamp $(SOCIAL_AVATARS) | $(SITE)
	$(PANDOC) $< -f markdown-smart --pdf-engine=typst --resource-path=$(SITE) -o $@
$(SITE)/whitepaper.de.pdf: docs/whitepaper.de.md web/site.mk $(SITE)/img/.stamp $(SOCIAL_AVATARS) | $(SITE)
	$(PANDOC) $< -f markdown-smart --pdf-engine=typst --resource-path=$(SITE) -o $@

# The whitepaper's figures, rendered from the scenes (tools/figures.py lists them).
$(SITE)/img/.stamp: tools/figures.py tools/flipbook.py build/preview | $(SITE)
	python3 tools/figures.py $(SITE)/img && touch $@

# The social avatar concepts are documentation assets, not rendered show scenes.
$(SOCIAL_AVATARS): $(SITE)/img/%: docs/img/% | $(SITE)
	@mkdir -p $(dir $@)
	cp $< $@

# The scenes as WebAssembly; fonts and slides travel inside the module. Needs fonts/*.psf,
# which `make` extracts on Debian/Ubuntu (console-setup) and the README explains for the Mac.
$(SITE)/bc101.js: $(WEB_SRCS) src/core.h src/scenes.h src/social.h src/dem0.h web/site.mk $(wildcard slides/*.txt) | $(SITE)
	@test -f fonts/big.psf -a -f fonts/small.psf || { echo "fonts/*.psf missing: run make (Linux) or copy them from the Pi (see README)" >&2; exit 1; }
	$(EMCC) -O2 -Wall -Isrc $(WEB_SRCS) -o $@ --embed-file fonts --embed-file slides \
	  -sEXPORTED_FUNCTIONS=$(WEB_EXPORTS) -sEXPORTED_RUNTIME_METHODS=cwrap,HEAPU8 \
	  -sMODULARIZE=1 -sEXPORT_NAME=createBC101 -sENVIRONMENT=web,node

$(SITE)/flipbook.html: tools/flipbook.en.json tools/flipbook.py tools/flipbook.tpl.html web/style.css build/preview | $(SITE)
	python3 tools/flipbook.py $< $@
$(SITE)/flipbook.de.html: tools/flipbook.de.json tools/flipbook.py tools/flipbook.tpl.html web/style.css build/preview | $(SITE)
	python3 tools/flipbook.py $< $@

$(SITE)/player.js $(SITE)/style.css $(SITE)/favicon.svg $(SITE)/robots.txt: $(SITE)/%: web/% | $(SITE)
	cp $< $@

$(SITE)/sitemap.xml: web/site.mk $(addprefix $(SITE)/,$(PAGES)) | $(SITE)
	{ echo '<?xml version="1.0" encoding="UTF-8"?>'; echo '<urlset xmlns="http://www.sitemaps.org/schemas/sitemap/0.9">'; \
	  for p in $(PAGES); do echo "  <url><loc>$(BASE)/$$p</loc><lastmod>$$(date -u +%F)</lastmod></url>"; done; \
	  echo '</urlset>'; } > $@

$(SITE):
	@mkdir -p $(SITE)

# Smoke test of the WebAssembly build: every scene draws, no frame is slow.
check-site: $(SITE)/bc101.js
	$(NODE) web/check.js $<

.PHONY: site check-site
