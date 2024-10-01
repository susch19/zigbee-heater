const fz = require('zigbee-herdsman-converters/converters/fromZigbee');
const tz = require('zigbee-herdsman-converters/converters/toZigbee');
const exposes = require('zigbee-herdsman-converters/lib/exposes');
const reporting = require('zigbee-herdsman-converters/lib/reporting');
const modernExtend = require('zigbee-herdsman-converters/lib/modernExtend');
const Zcl = require('zigbee-herdsman').Zcl;
const ota = require('zigbee-herdsman-converters/lib/ota') ;
const e = exposes.presets;
const ea = exposes.access;



function _getScheduleParameter() {
    parameters = [
        {name: 'transitions', type: Zcl.DataType.UINT8},
        {name: 'day_of_week', type: Zcl.DataType.BITMAP8},
        {name: 'mode', type: Zcl.DataType.BITMAP8},
    ];
    for (let index = 1; index < 11; index++) {
        parameters.push({name: `transition_time_${index}`, type: Zcl.DataType.UINT16});
        parameters.push({name: `set_point_${index}`, type: Zcl.DataType.INT16});
        // parameters.push({name: `cool_set_point_${index}`, type: Zcl.DataType.INT16});
    }
    return parameters;
}
function _getCustomScheduleParameter() {
    parameters = [
        {name: 'transitions', type: Zcl.DataType.UINT8},
        {name: 'mode', type: Zcl.DataType.BITMAP8},
    ];
    for (let index = 1; index < 11; index++) {
        parameters.push({name: `day_of_week_${index}`, type: Zcl.DataType.BITMAP8}),
        parameters.push({name: `transition_time_${index}`, type: Zcl.DataType.UINT16});
        parameters.push({name: `set_point_${index}`, type: Zcl.DataType.INT16});
    }
    return parameters;
}

const fzLocal = {
    current_target: {
        cluster: 'customThermostat',
        type: ['attributeReport', 'readResponse'],
        convert: (model, msg, publish, options, meta) => {
            const data = msg.data;
            const result = {};

            if(data.currentTarget !== undefined){

                const curTarget = data['currentTarget']; //z2m: Received Zigbee message from 'SmallESP', type 'attributeReport', cluster 'customThermostat', data '{"currentTarget":127796569}' from endpoint 1 with groupID 0
                
                const temp = (curTarget >> 16) ;
                const time = (curTarget & 0xFFFF);
                result.current_target = `${Math.floor(time/60)}:${time%60} Uhr, ${temp/100}°C` 
            }

            return result;
        },
    } ,
};

const definition = {
    zigbeeModel: ['heater'],
    model: 'heater',
    vendor: 'susch19',
    description: '',
    extend: [
        modernExtend.deviceAddCustomCluster('customThermostat', {
            ID: 0xff00,
            attributes: {
                runtimeSeconds: {ID: 0x0000, type: Zcl.DataType.UINT32},
                usedTemperatureSource: {ID: 0x0001, type: Zcl.DataType.ENUM8},
                currentTarget: {ID: 0x0002, type: Zcl.DataType.UINT32}
            },
            commands: {
                setpointRaiseLower: {
                    ID: 0x0,
                    parameters: [
                        {name: 'mode', type: Zcl.DataType.ENUM8},
                        {name: 'amount', type: Zcl.DataType.INT8},
                    ],
                },
                setWeeklySchedule: {
                    ID: 0x1,
                    parameters: _getScheduleParameter(),
                },
                getWeeklySchedule: {
                    ID: 0x2,
                    parameters: [
                        {name: 'days_to_return', type: Zcl.DataType.BITMAP8},
                        {name: 'mode_to_return', type: Zcl.DataType.BITMAP8},
                    ],
                },
                clearWeeklySchedule: {
                    ID: 0x3,
                    parameters: [],
                },
                setCustomWeeklySchedule: {
                    ID: 0xff,
                    parameters: _getCustomScheduleParameter(),
                },
            },
            commandsResponse: {
                getWeeklyScheduleResponse: {
                    ID: 0x00,
                    parameters: _getScheduleParameter(),
                },
            },
        }),

        modernExtend.enumLookup({
            name: 'system_mode',
            lookup: { heat:0x04 ,auto:0x01, off: 0x00},
            cluster: 0x201,
            attribute:  "systemMode",
            description: 'Mode of this device, heat and auto will both regulate based on the schedule.',
            access: 'ALL',
        }),
        modernExtend.temperature(),
        modernExtend.temperature({
            name: "heating_setpoint",
            cluster: 0x201,
            attribute:  {
                ID: 0x14,
                type: Zcl.DataType.INT16
            },
            description: "Temperature setpoint",
            access: 'ALL',
            unit: "°C",
            valueMin: 5,
            valueMax: 35,
            valueStep: 0.1

        }),
        modernExtend.enumLookup({
            name: 'running_mode',
            lookup:  { heat:0x04 , cool: 0x03, off: 0x00},
            cluster: 'hvacThermostat',
            attribute: 'runningMode',
            description: 'Describes if the heater is currently heating or off.',
            access: 'STATE',
        }),

        modernExtend.enumLookup({
            name: 'setpoint_change_source',
            lookup: {manual: 0x0,schedule: 0x1, extern: 0x2},
            cluster: 0x201,
            attribute:  {
                ID: 0x30,
                type: Zcl.DataType.ENUM8
            },
            description: 'Describes where the heater currently gets the temperature target from',
            access: 'STATE',
        }),
        modernExtend.numeric({
            name: 'runtime_seconds',
            cluster: 'customThermostat',
            attribute:  "runtimeSeconds",
            description: 'The total heating time in seconds',
            access: 'ALL',
            unit: "seconds"
        }),

        
        modernExtend.enumLookup({
            name: 'used_temperature_source',
            lookup: {none:0x0, local:0x1 ,remote:0x2},
            cluster: 'customThermostat',
            attribute:  "usedTemperatureSource",
            description: 'Source of the used temperature',
            access: 'STATE_GET',
        }),
        modernExtend.customTimeResponse('1970_UTC'),


    ],
    ota: ota.zigbeeOTA,
    fromZigbee: [fzLocal.current_target],
    exposes: [
        e.text('current_target', ea.STATE).withDescription('Current found schedule target'),],

};

module.exports = definition;
