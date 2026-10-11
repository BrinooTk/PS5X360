import assert from 'node:assert/strict';
import {skeleton} from './pose.mjs';
const points=Array.from({length:33},(_,i)=>({x:i,y:i+1,z:i+2,visibility:.8}));
const frame=skeleton(points,123);
assert.equal(frame.joints.length,20);
assert.equal(frame.joints[0].x,23.5);
assert.equal(frame.joints[4].x,11);
assert.equal(frame.joints[8].x,12);
assert.equal(frame.joints[4].y,-12);
assert.equal(frame.joints[4].z,-13);
assert.equal(frame.joints[1].x,17.5);
assert.throws(()=>skeleton([],1));
assert.throws(()=>skeleton(points,NaN));
points[15].x=Infinity;assert.throws(()=>skeleton(points,1));
console.log('PASS: joint order, interpolation, axis conversion, malformed input rejection');

// Browser-language variants, explicit choice, full messages and safe value substitution.
const {languageFor, translator, errorMessage} = await import('./i18n.mjs');
for (const locale of ['fr', 'fr-FR', 'fr-CA']) assert.equal(languageFor(null, [locale]), 'fr');
assert.equal(languageFor(null, ['en-US', 'fr']), 'en');
assert.equal(languageFor(null, ['de']), 'en');
assert.equal(languageFor('en', ['fr-FR']), 'en');
assert.equal(languageFor('fr', ['en-US']), 'fr');
assert.equal(languageFor('unknown', []), 'en');
const t=translator('fr');
assert.equal(t('Connect PS5'), 'Connecter la PS5');
assert.equal(t('PS5 rejected frame ({status}).', {status:409}), 'La PS5 a refusé les données de suivi (code 409).');
assert.equal(t('Game: taking the skeleton ({frames} frames, {bodies} with a body).', {frames:12,bodies:10}),
  'Jeu : squelette reçu (12 trames, dont 10 avec un corps détecté).');
assert.equal(t('PS5 rejected frame ({status}).', {status:'$&'}), 'La PS5 a refusé les données de suivi (code $&).');
assert.equal(translator('en')('Connect PS5'), 'Connect PS5');
assert.equal(t('An external error'), 'An external error');
assert.equal(errorMessage({name:'NotAllowedError'},t), 'L’accès à la caméra a été refusé. Autorisez-le dans le navigateur, puis réessayez.');
assert.equal(errorMessage({name:'Error',message:'Pair with the console first'},t), 'Connectez d’abord la PS5.');
console.log('PASS: French camera-page selection, messages, placeholders and error translations');
