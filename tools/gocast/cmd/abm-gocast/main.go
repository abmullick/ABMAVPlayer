package main

import (
	"context"
	"encoding/json"
	"fmt"
	"log"
	"path/filepath"
	"os"
	"strconv"
	"time"

	"github.com/tristanpenman/go-cast/internal/client"
)

type result struct { OK bool `json:"ok"`; Error string `json:"error,omitempty"`; ScreenID string `json:"screenId,omitempty"`; Availability string `json:"availability,omitempty"`; Launched bool `json:"launched,omitempty"` }

var diag = func() *log.Logger {
    f, err := os.OpenFile(filepath.Join(os.TempDir(), "ABMAVPlayer-GoCast-diagnostics.log"), os.O_CREATE|os.O_APPEND|os.O_WRONLY, 0600)
    if err != nil { return log.New(os.Stderr, "GoCast: ", log.LstdFlags|log.Lmicroseconds) }
    return log.New(f, "GoCast: ", log.LstdFlags|log.Lmicroseconds)
}()

func fail(err error) { diag.Printf("FAILED: %v", err); _ = json.NewEncoder(os.Stdout).Encode(result{OK:false,Error:err.Error()}) }

func main() {
	if len(os.Args)==5 && (os.Args[1]=="--check-app" || os.Args[1]=="--launch-app") {
		port,err:=strconv.Atoi(os.Args[3])
		if err!=nil || port<1 || port>65535 { fail(fmt.Errorf("invalid receiver port")); os.Exit(2) }
		appID:=os.Args[4]
		if len(appID)!=8 { fail(fmt.Errorf("invalid Cast application ID")); os.Exit(2) }
		for _,c:=range appID { if !(c>='0'&&c<='9'||c>='A'&&c<='F') { fail(fmt.Errorf("invalid Cast application ID")); os.Exit(2) } }
		launch:=os.Args[1]=="--launch-app"
		diag.Printf("checking Cast app id=%s on %s:%d launch=%t",appID,os.Args[2],port,launch)
		conn,err:=client.NewClient(os.Args[2],uint(port),true,nil)
		if err!=nil { fail(fmt.Errorf("GoCast receiver authentication/connection: %w",err)); os.Exit(1) }
		defer conn.Close()
		sender:=client.NewSender(conn,nil)
		sender.Connect()
		sender.RequestAppAvailability([]string{appID})
		availability,err:=sender.WaitForAvailability(8*time.Second)
		diag.Printf("Cast app id=%s availability response: %v",appID,availability)
		if err!=nil { fail(fmt.Errorf("query Cast app %s availability: %w",appID,err)); os.Exit(1) }
		value:=availability[appID]
		if !launch || value!="APP_AVAILABLE" {
			_ = json.NewEncoder(os.Stdout).Encode(result{OK:true,Availability:value})
			if launch && value!="APP_AVAILABLE" { os.Exit(1) }
			return
		}
		sender.LaunchApp(appID)
		transport,err:=sender.WaitForAppTransport(appID,20*time.Second)
		if err!=nil { fail(fmt.Errorf("wait for Cast app %s launch: %w",appID,err)); os.Exit(1) }
		sender.ConnectTransport(transport)
		diag.Printf("Cast app id=%s launch confirmed; transport=%s",appID,transport)
		_ = json.NewEncoder(os.Stdout).Encode(result{OK:true,Availability:"APP_AVAILABLE",Launched:true})
		return
	}
	if len(os.Args)==5 && os.Args[1]=="--app-status" {
		port,err:=strconv.Atoi(os.Args[3])
		if err!=nil || port<1 || port>65535 { fail(fmt.Errorf("invalid receiver port")); os.Exit(2) }
		appID:=os.Args[4]
		if len(appID)!=8 { fail(fmt.Errorf("invalid Cast application ID")); os.Exit(2) }
		for _,c:=range appID { if !(c>='0'&&c<='9'||c>='A'&&c<='F') { fail(fmt.Errorf("invalid Cast application ID")); os.Exit(2) } }
		diag.Printf("checking running Cast app id=%s on %s:%d",appID,os.Args[2],port)
		conn,err:=client.NewClient(os.Args[2],uint(port),true,nil)
		if err!=nil { fail(fmt.Errorf("GoCast receiver authentication/connection: %w",err)); os.Exit(1) }
		defer conn.Close()
		sender:=client.NewSender(conn,nil)
		sender.Connect()
		sender.RequestStatus()
		status,err:=sender.WaitForStatus(8*time.Second)
		if err!=nil { fail(fmt.Errorf("query running Cast applications: %w",err)); os.Exit(1) }
		for _,app:=range status.Applications {
			if app.AppID==appID {
				_ = json.NewEncoder(os.Stdout).Encode(result{OK:true,Availability:"APP_RUNNING"})
				return
			}
		}
		_ = json.NewEncoder(os.Stdout).Encode(result{OK:true,Availability:"APP_NOT_RUNNING"})
		return
	}
	if len(os.Args)==5 && os.Args[1]=="--stop-app" {
		port,err:=strconv.Atoi(os.Args[3])
		if err!=nil || port<1 || port>65535 { fail(fmt.Errorf("invalid receiver port")); os.Exit(2) }
		appID:=os.Args[4]
		if len(appID)!=8 { fail(fmt.Errorf("invalid Cast application ID")); os.Exit(2) }
		for _,c:=range appID { if !(c>='0'&&c<='9'||c>='A'&&c<='F') { fail(fmt.Errorf("invalid Cast application ID")); os.Exit(2) } }
		diag.Printf("stopping Cast app id=%s on %s:%d",appID,os.Args[2],port)
		conn,err:=client.NewClient(os.Args[2],uint(port),true,nil)
		if err!=nil { fail(fmt.Errorf("GoCast receiver authentication/connection: %w",err)); os.Exit(1) }
		defer conn.Close()
		sender:=client.NewSender(conn,nil)
		sender.Connect()
		sender.RequestStatus()
		status,err:=sender.WaitForStatus(8*time.Second)
		if err!=nil { fail(fmt.Errorf("query running Cast applications: %w",err)); os.Exit(1) }
		var sessionID string
		for _,app:=range status.Applications { if app.AppID==appID && app.SessionID!="" { sessionID=app.SessionID; break } }
		if sessionID=="" {
			_ = json.NewEncoder(os.Stdout).Encode(result{OK:true,Availability:"APP_NOT_RUNNING"})
			return
		}
		sender.StopApp(sessionID)
		if err:=sender.WaitForAppStopped(appID,8*time.Second); err!=nil {
			fail(fmt.Errorf("receiver did not confirm app stop: %w",err)); os.Exit(1)
		}
		_ = json.NewEncoder(os.Stdout).Encode(result{OK:true,Availability:"APP_STOPPED"})
		return
	}
	if len(os.Args)==4 && os.Args[1]=="--check-youtube" {
		port,err:=strconv.Atoi(os.Args[3])
		if err!=nil || port<1 || port>65535 { fail(fmt.Errorf("invalid receiver port")); os.Exit(2) }
		diag.Printf("checking YouTube Cast app availability on %s:%d",os.Args[2],port)
		conn,err:=client.NewClient(os.Args[2],uint(port),true,nil)
		if err!=nil { fail(fmt.Errorf("GoCast receiver authentication/connection: %w",err)); os.Exit(1) }
		defer conn.Close()
		sender:=client.NewSender(conn,nil)
		sender.Connect()
		sender.RequestAppAvailability([]string{"233637DE"})
		availability,err:=sender.WaitForAvailability(8*time.Second)
		diag.Printf("YouTube app availability response: %v",availability)
		if err!=nil { fail(fmt.Errorf("query YouTube app availability: %w",err)); os.Exit(1) }
		value:=availability["233637DE"]
		_ = json.NewEncoder(os.Stdout).Encode(result{OK:true,Availability:value})
		return
	}
	if len(os.Args)==4 && os.Args[1]=="--launch-youtube" {
		port,err:=strconv.Atoi(os.Args[3])
		if err!=nil || port<1 || port>65535 { fail(fmt.Errorf("invalid receiver port")); os.Exit(2) }
		diag.Printf("launching YouTube TV app on %s:%d",os.Args[2],port)
		conn,err:=client.NewClient(os.Args[2],uint(port),true,nil)
		if err!=nil { fail(fmt.Errorf("GoCast receiver authentication/connection: %w",err)); os.Exit(1) }
		defer conn.Close()
		sender:=client.NewSender(conn,nil)
		sender.Connect()
		sender.RequestAppAvailability([]string{"233637DE"})
		availability,err:=sender.WaitForAvailability(8*time.Second)
		if err!=nil { fail(fmt.Errorf("query YouTube app availability: %w",err)); os.Exit(1) }
		if availability["233637DE"]!="APP_AVAILABLE" {
			_ = json.NewEncoder(os.Stdout).Encode(result{OK:false,Availability:availability["233637DE"],Error:"YouTube Cast app is not available on this receiver"})
			os.Exit(1)
		}
		sender.LaunchApp("233637DE")
		transport,err:=sender.WaitForAppTransport("233637DE",20*time.Second)
		if err!=nil { fail(fmt.Errorf("wait for YouTube app launch: %w",err)); os.Exit(1) }
		sender.ConnectTransport(transport)
		diag.Printf("YouTube TV app launch confirmed; transport=%s",transport)
		_ = json.NewEncoder(os.Stdout).Encode(result{OK:true,Availability:"APP_AVAILABLE",Launched:true})
		return
	}
	if len(os.Args)!=4 { fail(fmt.Errorf("usage: abm-gocast.exe <receiver-host> <port> <youtube-video-id>")); os.Exit(2) }
	port,err:=strconv.Atoi(os.Args[2]); if err!=nil || port<1 || port>65535 { fail(fmt.Errorf("invalid receiver port")); os.Exit(2) }
	video:=os.Args[3]
	if len(video)!=11 { fail(fmt.Errorf("invalid YouTube video ID")); os.Exit(2) }
	for _,c:=range video { if !(c>='a'&&c<='z'||c>='A'&&c<='Z'||c>='0'&&c<='9'||c=='_'||c=='-') { fail(fmt.Errorf("invalid YouTube video ID")); os.Exit(2) } }

	diag.Printf("connecting to receiver %s:%d", os.Args[1], port)
	conn,err:=client.NewClient(os.Args[1],uint(port),true,nil)
	if err!=nil { fail(fmt.Errorf("GoCast receiver authentication/connection: %w",err)); os.Exit(1) }
	defer conn.Close()
	sender:=client.NewSender(conn,nil)
	diag.Printf("receiver connection established; sending Cast CONNECT")
	sender.Connect()
	sender.RequestAppAvailability([]string{"233637DE"})
	diag.Printf("requested YouTube Cast app availability")
	availability,err:=sender.WaitForAvailability(8*time.Second)
	diag.Printf("YouTube app availability response: %v", availability)
	if err!=nil { fail(fmt.Errorf("query YouTube app availability: %w",err)); os.Exit(1) }
	if availability["233637DE"]!="APP_AVAILABLE" { fail(fmt.Errorf("receiver does not advertise the YouTube Cast app (233637DE)")); os.Exit(1) }

	diag.Printf("launching YouTube Cast app")
	sender.LaunchApp("233637DE")
	transport,err:=sender.WaitForAppTransport("233637DE",20*time.Second)
	if err!=nil { fail(fmt.Errorf("wait for YouTube Cast session: %w",err)); os.Exit(1) }
	diag.Printf("YouTube app transport acquired; connecting")
	sender.ConnectTransport(transport)
	diag.Printf("requesting YouTube MDX screen ID")
	screen,err:=sender.RequestYouTubeScreenID(transport,12*time.Second)
	if err!=nil { fail(fmt.Errorf("get YouTube MDX screen ID: %w",err)); os.Exit(1) }
	ctx,cancel:=context.WithTimeout(context.Background(),20*time.Second); defer cancel()
	diag.Printf("MDX screen ID acquired; beginning YouTube Lounge handoff")
	if err:=client.PlayYouTubeViaLounge(ctx,screen,video); err!=nil { fail(fmt.Errorf("YouTube Lounge handoff: %w",err)); os.Exit(1) }
	diag.Printf("YouTube Lounge handoff completed successfully")
	_ = json.NewEncoder(os.Stdout).Encode(result{OK:true,ScreenID:screen})
}
