MANUAL_SRC = manual.md
OUTDIR = ../../_site

MANUAL_HTML = $(OUTDIR)/manual.html
MANUAL_PDF = $(OUTDIR)/manual.pdf

# R&D notes the manual links to, rendered into the site alongside manual.html.
NOTES_SRC = ../notes/clipboard-dnd.md
NOTES_HTML = $(OUTDIR)/clipboard-dnd.html

# The lilpc sample spec, rendered into the site alongside manual.html.
SPEC_SRC = ../../samples/lilpc/lilpc-spec.md
SPEC_HTML = $(OUTDIR)/lilpc-spec.html

all: html

html: $(MANUAL_HTML) $(NOTES_HTML) $(SPEC_HTML)
pdf: $(MANUAL_PDF)

$(OUTDIR):
	mkdir -p $(OUTDIR)

$(MANUAL_HTML): $(MANUAL_SRC) style.css | $(OUTDIR)
	pandoc $< \
		-f markdown -t html \
		--standalone \
		--metadata title="Game Engine Manual" \
		--css style.css \
		-o $@
	cp style.css $(OUTDIR)/

$(NOTES_HTML): $(NOTES_SRC) style.css | $(OUTDIR)
	pandoc $< \
		-f markdown -t html \
		--standalone \
		--metadata title="Clipboard and Drag-and-Drop Notes" \
		--css style.css \
		-o $@
	cp style.css $(OUTDIR)/

$(SPEC_HTML): $(SPEC_SRC) style.css | $(OUTDIR)
	pandoc $< \
		-f markdown -t html \
		--standalone --embed-resources \
		--resource-path=$(dir $(SPEC_SRC)) \
		--metadata title="lilpc Specification" \
		--css style.css \
		-o $@
	cp style.css $(OUTDIR)/

$(MANUAL_PDF): $(MANUAL_SRC) | $(OUTDIR)
	pandoc $< -o $@

clean:
	rm -f $(MANUAL_HTML) $(NOTES_HTML) $(SPEC_HTML) $(MANUAL_PDF) $(OUTDIR)/style.css

.PHONY: all html pdf clean
