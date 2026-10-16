// module_config.pb.h: PWM uses device.buzzer_gpio and ignores these settings.
export function moduleFieldAvailable(model,path){
 if(path.length!==2||path[0]!=='external_notification'||!['output','output_ms','active'].includes(path[1]))return true;
 const notification=model.fields.find(field=>field.name==='external_notification')?.value;
 return notification?.fields.find(field=>field.name==='use_pwm')?.value!==true;
}
