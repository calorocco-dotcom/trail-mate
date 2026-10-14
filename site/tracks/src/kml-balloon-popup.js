import {previewKmlBalloon} from './kml-balloon.js';
import {kmlBalloonText} from '../../i18n/kml-balloon-labels.js';
const css=value=>value.color+Math.round(value.opacity*255).toString(16).padStart(2,'0');
export function createKmlBalloonPopup(model,index,fields,locale){
 if(!fields||fields.displayMode==='hide')return null;
 const text=kmlBalloonText(locale),root=document.createElement('div');root.className='kml-balloon-popup';
 try{const preview=previewKmlBalloon(model,index,fields),content=document.createElement('pre');content.className='kml-balloon-preview';content.style.backgroundColor=css(preview.background);content.style.color=css(preview.foreground);content.textContent=preview.text;root.append(content);const note=document.createElement('small');note.textContent=text.plain;root.append(note);if(preview.unknown.length){const warning=document.createElement('p');warning.textContent=text.unknown.replace('{tokens}',preview.unknown.join(', '));root.append(warning);}}
 catch{root.textContent=text.limited;}
 return root;
}
