function topLevelWindow() {
  return window.top === window.self;
}

export function setupPwa({ installButton, onLaunchFile }) {
  if (!topLevelWindow()) {
    return;
  }

  let installPrompt = null;

  window.addEventListener('beforeinstallprompt', (event) => {
    event.preventDefault();
    installPrompt = event;
    installButton.hidden = false;
  });

  installButton.addEventListener('click', async () => {
    if (!installPrompt) {
      return;
    }
    const prompt = installPrompt;
    installPrompt = null;
    installButton.hidden = true;
    await prompt.prompt();
    await prompt.userChoice;
  });

  window.addEventListener('appinstalled', () => {
    installPrompt = null;
    installButton.hidden = true;
  });

  if ('launchQueue' in window) {
    window.launchQueue.setConsumer(async ({ files }) => {
      const handle = files?.[0];
      if (!handle) {
        return;
      }
      try {
        await onLaunchFile(await handle.getFile());
      } catch (error) {
        console.warn('Could not open the launched CSV file.', error);
      }
    });
  }

  if (import.meta.env.PROD && 'serviceWorker' in navigator) {
    window.addEventListener('load', () => {
      navigator.serviceWorker.register('./sw.js', { scope: './' })
        .catch((error) => console.warn('Service worker registration failed.', error));
    });
  }
}
