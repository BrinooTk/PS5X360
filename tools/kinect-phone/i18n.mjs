// SPDX-License-Identifier: MIT
// Keep complete messages together so translations can place values naturally.
const french = {
  'Language': 'Langue',
  'Camera motion research. Sends your pose to PS5X360, which hands it to a Kinect game as its skeleton. Experimental.': 'Suivi des mouvements par caméra, à titre expérimental. Transmet votre posture à PS5X360 pour qu’un jeu Kinect puisse l’utiliser comme squelette.',
  'PS5 pairing key': 'Code de connexion PS5',
  'Six characters': 'Six caractères',
  'Connect PS5': 'Connecter la PS5',
  'Disconnect PS5': 'Déconnecter la PS5',
  'Console disconnected.': 'Console déconnectée.',
  'Start camera': 'Activer la caméra',
  'Stop': 'Arrêter',
  'Export pose': 'Exporter la posture',
  'Camera off. No connection to the emulator.': 'Caméra désactivée. Aucune connexion à l’émulateur.',
  'Processing stays in this browser. Models download from Google and jsDelivr. No video upload. Pose transmission to your LAN PS5 is opt-in. Use HTTPS on your phone, or localhost on a computer.': 'Le traitement s’effectue dans ce navigateur. Les modèles sont téléchargés depuis Google et jsDelivr. Aucune vidéo n’est envoyée. Les données de posture ne sont transmises à la PS5 sur votre réseau local que si vous activez la connexion. Utilisez HTTPS sur un téléphone, ou localhost sur un ordinateur.',
  'Game: taking the skeleton ({frames} frames, {bodies} with a body).': 'Jeu : squelette reçu ({frames} trames, dont {bodies} avec un corps détecté).',
  'Game: camera opened, but its skeleton function was not found.': 'Jeu : caméra ouverte, mais la fonction de lecture du squelette n’a pas été trouvée.',
  'Game: has not opened the camera (no Kinect game running, or still loading).': 'Jeu : caméra non ouverte. Aucun jeu Kinect n’est en cours, ou son chargement n’est pas terminé.',
  'Reconnect the console to renew the motion session.': 'Reconnectez la console pour démarrer une nouvelle session de suivi.',
  'Pairing expired. Enter the current key shown on the PS5 and click Connect PS5.': 'La connexion a expiré. Saisissez le code actuellement affiché sur la PS5, puis cliquez sur Connecter la PS5.',
  'PS5 connection lost. Reopen the emulator and check the settings-page port.': 'Connexion à la PS5 perdue. Relancez l’émulateur et vérifiez le port de sa page de paramètres.',
  'PS5 rejected frame ({status}).': 'La PS5 a refusé les données de suivi (code {status}).',
  'PS5 receiving 20 joints.': 'La PS5 reçoit la position des 20 articulations.',
  'PS5 receiving body lost.': 'Corps non détecté. La PS5 en a été informée.',
  'Pairing failed': 'Connexion impossible',
  'Enable Camera motion (research) on the PS5 first.': 'Activez d’abord Suivi par caméra (expérimental) dans les paramètres de PS5X360.',
  'Paired. Start camera to send joints; no video is sent.': 'PS5 connectée. Activez la caméra pour transmettre la position des articulations. Aucune vidéo n’est envoyée.',
  'Console disconnected; pose expires within 500 ms.': 'Console déconnectée. La dernière posture sera invalidée sous 500 ms.',
  'Camera off.': 'Caméra désactivée.',
  'Camera requires HTTPS or localhost.': 'L’accès à la caméra nécessite HTTPS ou une adresse localhost.',
  'Loading pose model…': 'Chargement du modèle de suivi…',
  'Body detected — camera tracking active.': 'Corps détecté. Suivi des mouvements actif.',
  'Stand back so your full body is visible.': 'Reculez pour que votre corps soit visible en entier.',
  'Pairing expired. Enter the current key shown on the PS5 and reconnect.': 'La connexion a expiré. Saisissez le code actuellement affiché sur la PS5, puis reconnectez-la.',
  'Console rejected request': 'La console a refusé la demande.',
  'Console unavailable; check address, web page and build version': 'Console inaccessible. Vérifiez son adresse, l’activation de sa page de paramètres et la version de l’émulateur.',
  'Loopback host required': 'Ouvrez cette page sur l’ordinateur qui héberge le relais, à son adresse localhost.',
  'Local camera page required': 'Utilisez la page caméra servie par le relais local.',
  'Pair with the console first': 'Connectez d’abord la PS5.',
  'Unknown route': 'Cette adresse n’est pas disponible.',
  'Invalid research frame or pairing request': 'Les données de suivi ou le code de connexion sont invalides.',
  'Camera access was denied. Allow camera access in your browser and try again.': 'L’accès à la caméra a été refusé. Autorisez-le dans le navigateur, puis réessayez.',
  'No camera found. Connect a camera and try again.': 'Aucune caméra détectée. Branchez une caméra, puis réessayez.',
  'Camera unavailable. Close other apps using it and try again.': 'Caméra indisponible. Fermez les applications qui l’utilisent, puis réessayez.',
  'Network request failed. Check your connection and that the local relay is running.': 'Échec de la connexion. Vérifiez votre réseau et assurez-vous que le relais local est lancé.',
};

export function languageFor(requested, browserLanguages = []) {
  if (requested === 'en' || requested === 'fr') return requested;
  const language = browserLanguages[0] || 'en';
  return /^fr(?:-|$)/i.test(language) ? 'fr' : 'en';
}

export function translator(language) {
  return (message, values = {}) => (language === 'fr' ? french[message] || message : message)
    .replace(/\{(\w+)\}/g, (match, key) => Object.hasOwn(values, key) ? String(values[key]) : match);
}

export function errorMessage(error, t) {
  if (error.name === 'NotAllowedError' || error.name === 'SecurityError')
    return t('Camera access was denied. Allow camera access in your browser and try again.');
  if (error.name === 'NotFoundError') return t('No camera found. Connect a camera and try again.');
  if (error.name === 'NotReadableError') return t('Camera unavailable. Close other apps using it and try again.');
  if (error.name === 'TypeError' && /fetch|network|load failed/i.test(error.message))
    return t('Network request failed. Check your connection and that the local relay is running.');
  return t(error.message);
}
