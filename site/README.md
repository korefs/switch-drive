# Switch Drive public site

This directory is a static site with the application homepage, privacy policy,
and terms of service. It has no build step.

## Publish with Cloudflare Pages

1. In Cloudflare, open **Workers & Pages → Create → Pages → Connect to Git**.
2. Select this repository and use these build settings:
   - Production branch: `main`
   - Framework preset: `None`
   - Build command: leave empty
   - Build output directory: `site`
   - Root directory: leave at the repository root
3. Deploy, then open **Custom domains** for the Pages project and add
   `swdrive.erok.qzz.io`.
4. Confirm these public URLs before submitting Google OAuth verification:
   - `https://swdrive.erok.qzz.io/`
   - `https://swdrive.erok.qzz.io/privacy/`
   - `https://swdrive.erok.qzz.io/terms/`

Keep the API separate at `https://api.erok.qzz.io`; the OAuth redirect URI is
`https://api.erok.qzz.io/oauth/google/callback`.
