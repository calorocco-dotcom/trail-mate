import React from 'react';
import {Card} from 'animal-island-ui';
import {configurationReview} from './config-review.js';
import {configReviewText} from '../../i18n/config-review-labels.js';
export function ConfigReview({document,baseline,locale,labels}){
 const rows=configurationReview(document,baseline,locale),text={...configReviewText(locale),...labels};
 if(!rows.length)return null;
 return <Card className="device-review"><h2>{text.title}</h2><div className="device-review-scroll"><table><thead><tr><th>{text.field}</th><th>{text.before}</th><th>{text.after}</th></tr></thead>
  <tbody>{rows.map(row=><tr key={row.key}><th scope="row">{row.label}<code dir="ltr">{row.key}</code></th><td><bdi>{row.before}</bdi></td><td><bdi>{row.after}</bdi></td></tr>)}</tbody>
 </table></div></Card>;
}
