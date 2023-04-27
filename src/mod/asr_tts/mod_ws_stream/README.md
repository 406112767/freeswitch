This is a module to recognize speech using ws server. 

To use this server with freeswitch:

  1. Make sure you have libks installed
  1. Configure and install freeswitch including `mod_ws_stream.so`
  1. Make sure mod_ws_stream.so is enabled in `modules.conf.xml` and `conf/ws_stream.conf.xml` is placed in autoload_configs

Run the following sample dialplan:

```
<include>
  <context name="default">
    <extension name="asr_demo">
        <condition field="destination_number" expression="^.*$">
          <action application="answer"/>
          <action application="play_and_detect_speech" data="ivr/ivr-welcome.wav detect:ws_stream default"/>
          <action application="speak" data="tts_commandline|espeak|You said ${detect_speech_result}!"/>
        </condition>
    </extension>
  </context>
</include>
```

!!!! ATTENTION, for reliable work this module requires several fixes in libks which are not yet merged, so rebuild libks from this:

You can create more advanced dialplans with ESL and scripts in various languages. See examples in scripts folder.

!!! ATTENTION In order for ESL to recieve events, make sure that fire_asr_events variable is set to true (false by default).
The dialplan can look like this:

```
<include>
  <context name="default">
    <extension name="asr_demo">
        <condition field="destination_number" expression="^.*$">
          <action application="answer"/>
          <action application="set" data="fire_asr_events=true"/>
          <action application="detect_speech" data="ws_stream default default"/>
          <action application="sleep" data="10000000"/>
        </condition>
    </extension>
  </context>
</include>
```
