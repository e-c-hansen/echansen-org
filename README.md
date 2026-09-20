# echansen.org

My personal site: a handful of hand-written HTML pages and one stylesheet,
published to GitHub Pages. No build step, no framework, no dependencies.

## Structure

```
├── .github/workflows/
│   └── pages.yml       # Publishes public/ to GitHub Pages on every push to main
├── assets/
│   └── resume.tex      # Original LaTeX resume source (reference only, not published)
└── public/             # Everything that gets served
    ├── index.html      # Landing page
    ├── resume.html     # Experience & skills
    ├── github.html     # Code & systems
    ├── style.css       # The whole design system, including the print stylesheet
    ├── resume.md       # Plain-text resume
    ├── favicon.svg     # Source for the site monogram
    ├── favicon.ico     # Multi-size legacy fallback
    ├── apple-touch-icon.png
    ├── CNAME           # Custom domain
    └── .nojekyll       # Keeps GitHub from running Jekyll over the files
```

## Local preview

No build, so any static server works:

```bash
python3 -m http.server 8080 -d public
```

Then open http://localhost:8080.

## Deploying

Push to `main`. The workflow uploads `public/` and deploys it. Pages must be
set to **Settings → Pages → Source = GitHub Actions**.

`resume.html` carries an `@media print` layout that reflows the page into a
clean one-page PDF when printed — worth re-checking after any resume edit.
