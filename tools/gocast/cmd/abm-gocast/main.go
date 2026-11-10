package main

import (
	"context"
	"encoding/json"
	"fmt"
	"os"
	"strconv"
	"time"

	"github.com/tristanpenman/go-cast/internal/client"
)

type result struct { OK bool `json:"ok"`; Error string `json:"error,omitempty"`; ScreenID string `json:"screenId,omitempty"` }

func fail(err error) { _ = json.NewEncoder(os.Stdout).Encode(result{OK:false,Error:err.Error()}) }

func main() {
	if len(os.Args)!=4 { fail(fmt.Errorf("usage: abm-gocast.exe <receiver-host> <port> <youtube-video-id>")); os.Exit(2) }
	port,err:=strconv.Atoi(os.Args[2]); if err!=nil || port<1 || port>65535 { fail(fmt.Errorf("invalid receiver port")); os.Exit(2) }
	video:=os.Args[3]
	if len(video)!=11 { fail(fmt.Errorf("invalid YouTube video ID")); os.Exit(2) }
	for _,c:=range video { if !(c>='a'&&c<='z'||c>='A'&&c<='Z'||c>='0'&&c<='9'||c=='_'||c=='-') { fail(fmt.Errorf("invalid YouTube video ID")); os.Exit(2) } }

	conn,err:=client.NewClient(os.Args[1],uint(port),true,nil)
	if err!=nil { fail(fmt.Errorf("GoCast receiver authentication/connection: %w",err)); os.Exit(1) }
	defer conn.Close()
	sender:=client.NewSender(conn,nil)
	sender.Connect()
	sender.RequestAppAvailability([]string{"233637DE"})
	availability,err:=sender.WaitForAvailability(8*time.Second)
	if err!=nil { fail(fmt.Errorf("query YouTube app availability: %w",err)); os.Exit(1) }
	if availability["233637DE"]!="APP_AVAILABLE" { fail(fmt.Errorf("receiver does not advertise the YouTube Cast app (233637DE)")); os.Exit(1) }

	sender.LaunchApp("233637DE")
	transport,err:=sender.WaitForAppTransport("233637DE",20*time.Second)
	if err!=nil { fail(fmt.Errorf("wait for YouTube Cast session: %w",err)); os.Exit(1) }
	sender.ConnectTransport(transport)
	screen,err:=sender.RequestYouTubeScreenID(transport,12*time.Second)
	if err!=nil { fail(fmt.Errorf("get YouTube MDX screen ID: %w",err)); os.Exit(1) }
	ctx,cancel:=context.WithTimeout(context.Background(),20*time.Second); defer cancel()
	if err:=client.PlayYouTubeViaLounge(ctx,screen,video); err!=nil { fail(fmt.Errorf("YouTube Lounge handoff: %w",err)); os.Exit(1) }
	_ = json.NewEncoder(os.Stdout).Encode(result{OK:true,ScreenID:screen})
}
