/*
	 This example code is in the Public Domain (or CC0 licensed, at your option.)

	 Unless required by applicable law or agreed to in writing, this
	 software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
	 CONDITIONS OF ANY KIND, either express or implied.
*/

#include <stdio.h>
#include <inttypes.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "esp_wifi.h"
#include "esp_log.h"

#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "ping/ping_sock.h"
#include "core.h"

static const char *TAG = "PING";

#define PING_COUNT 2
//ESP_EVENT_DEFINE_BASE(LED_EVENTS);

typedef struct {
	char *ip;
	uint8_t time;
	uint8_t *cont;
} ping_cfg_t;

esp_ping_handle_t ping;
uint8_t *continious;



void cmd_ping_on_ping_success(esp_ping_handle_t hdl, void *args)
{
	//uint8_t ttl;
	uint16_t seqno;
	//uint32_t elapsed_time, recv_len;
	//ip_addr_t target_addr;
	esp_ping_get_profile(hdl, ESP_PING_PROF_SEQNO, &seqno, sizeof(seqno));
	/*
	esp_ping_get_profile(hdl, ESP_PING_PROF_TTL, &ttl, sizeof(ttl));
	esp_ping_get_profile(hdl, ESP_PING_PROF_IPADDR, &target_addr, sizeof(target_addr));
	esp_ping_get_profile(hdl, ESP_PING_PROF_SIZE, &recv_len, sizeof(recv_len));
	esp_ping_get_profile(hdl, ESP_PING_PROF_TIMEGAP, &elapsed_time, sizeof(elapsed_time));

	ESP_LOGI(TAG, "%"PRIu32" bytes from %s icmp_seq=%d ttl=%d time=%"PRIu32" ms",
			 recv_len, inet_ntoa(target_addr.u_addr.ip4), seqno, ttl, elapsed_time);
    */ 
    if (seqno>PING_COUNT) {
	  esp_ping_stop(hdl);
	}

}

void cmd_ping_on_ping_timeout(esp_ping_handle_t hdl, void *args)
{
	uint16_t seqno;
	//ip_addr_t target_addr;
	esp_ping_get_profile(hdl, ESP_PING_PROF_SEQNO, &seqno, sizeof(seqno));
	/*
	esp_ping_get_profile(hdl, ESP_PING_PROF_IPADDR, &target_addr, sizeof(target_addr));
	ESP_LOGW(TAG, "From %s icmp_seq=%d timeout", inet_ntoa(target_addr.u_addr.ip4), seqno);
	*/
	if (seqno>PING_COUNT) {
	  esp_ping_stop(hdl);
	}
}

void cmd_ping_on_ping_end(esp_ping_handle_t hdl, void *args)
{
	ip_addr_t target_addr;
	uint32_t transmitted;
	uint32_t received;
	//uint32_t total_time_ms;
	esp_ping_get_profile(hdl, ESP_PING_PROF_REQUEST, &transmitted, sizeof(transmitted));
	esp_ping_get_profile(hdl, ESP_PING_PROF_REPLY, &received, sizeof(received));
	//esp_ping_get_profile(hdl, ESP_PING_PROF_IPADDR, &target_addr, sizeof(target_addr));
	//esp_ping_get_profile(hdl, ESP_PING_PROF_DURATION, &total_time_ms, sizeof(total_time_ms));
	uint32_t loss = (uint32_t)((1 - ((float)received) / transmitted) * 100);
	/*
	if (IP_IS_V4(&target_addr)) {
		ESP_LOGI(TAG, "\n--- %s ping statistics ---", inet_ntoa(*ip_2_ip4(&target_addr)));
	} else {
		ESP_LOGI(TAG, "\n--- %s ping statistics ---", inet6_ntoa(*ip_2_ip6(&target_addr)));
	}
	ESP_LOGI(TAG, "%"PRIu32" packets transmitted, %"PRIu32" received, %"PRIu32"%% packet loss, time %"PRIu32"ms",
			 transmitted, received, loss, total_time_ms);
			 */
	// delete the ping sessions, so that we clean up all resources and can create a new ping session
	// we don't have to call delete function in the callback, instead we can call delete function from other tasks
	if (loss>0) ESP_LOGE(TAG,"Packet Loss %%%lu %lu %lu",loss, transmitted, received);
	//esp_ping_delete_session(hdl);
	led_events_data_t ld={};
    if (loss==100) ld.state = 1; else ld.state = 0;
	if (loss<0 || transmitted==0) ld.state = 1;
	ESP_ERROR_CHECK(esp_event_post(LED_EVENTS, LED_EVENTS_PING, &ld, sizeof(led_events_data_t), portMAX_DELAY));
}

/*
ping to targer forever
interval_ms:ping interval mSec. Default is 1000mSec.
task_prio:ping task priority. Default is 2.
target_host:target host url. if null,target is own gateway.
*/
esp_err_t initialize_ping(uint32_t interval_ms, uint32_t task_prio, char * target_host)
{
	esp_ping_config_t ping_config = ESP_PING_DEFAULT_CONFIG();

	if (strlen(target_host) > 0) {
		/* convert URL to IP address */
		ip_addr_t target_addr;
		memset(&target_addr, 0, sizeof(target_addr));
		struct addrinfo hint;
		memset(&hint, 0, sizeof(hint));
		struct addrinfo *res = NULL;
		//int err = getaddrinfo("www.espressif.com", NULL, &hint, &res);
		int err = getaddrinfo(target_host, NULL, &hint, &res);
		if(err != 0 || res == NULL) {
			ESP_LOGE(TAG, "DNS lookup failed err=%d res=%p", err, res);
			return ESP_FAIL;
		} else {
			ESP_LOGI(TAG, "DNS lookup success");
		}

		if (res->ai_family == AF_INET) {
			struct in_addr addr4 = ((struct sockaddr_in *) (res->ai_addr))->sin_addr;
			inet_addr_to_ip4addr(ip_2_ip4(&target_addr), &addr4);
		} else {
			struct in6_addr addr6 = ((struct sockaddr_in6 *) (res->ai_addr))->sin6_addr;
			inet6_addr_to_ip6addr(ip_2_ip6(&target_addr), &addr6);
		}
		freeaddrinfo(res);
		ESP_LOGI(TAG, "target_addr.type=%d", target_addr.type);
		ESP_LOGI(TAG, "target_addr.u_addr.ip4=%s", ip4addr_ntoa(&(target_addr.u_addr.ip4)));
		ping_config.target_addr = target_addr; // target IP address
	} else {
		// ping target is my gateway
		//tcpip_adapter_ip_info_t ip_info;
		//ESP_ERROR_CHECK(tcpip_adapter_get_ip_info(TCPIP_ADAPTER_IF_STA, &ip_info));
		//ESP_LOGI(TAG, "IP Address: %s", ip4addr_ntoa(&ip_info.ip));
		//ESP_LOGI(TAG, "Subnet mask: %s", ip4addr_ntoa(&ip_info.netmask));
		//ESP_LOGI(TAG, "Gateway:	%s", ip4addr_ntoa(&ip_info.gw));

		// get gateway address in esp_netif_ip_info_t
		esp_netif_ip_info_t ip_info;
		ESP_ERROR_CHECK(esp_netif_get_ip_info(esp_netif_get_handle_from_ifkey("WIFI_STA_DEF"), &ip_info));
		ESP_LOGD(TAG, "ip_info.ip=" IPSTR, IP2STR(&ip_info.ip));
		ESP_LOGD(TAG, "ip_info.netmask=" IPSTR, IP2STR(&ip_info.netmask));
		ESP_LOGI(TAG, "ip_info.gw=" IPSTR, IP2STR(&ip_info.gw));

		// convert from esp_ip4_addr_t to string like "192.168.10.1"
		char buf[32];
		sprintf(buf, IPSTR, IP2STR(&ip_info.gw));

		// convert from string to ip_addr_t
		ip_addr_t gateway_addr;
		ip4addr_aton(buf, &gateway_addr.u_addr.ip4);

		// set lwip_ip_addr_type
		gateway_addr.type = IPADDR_TYPE_V4;
		//gateway_addr.u_addr.ip4 = ip_info.gw;
		//gateway_addr = ip_info.gw;
		ESP_LOGI(TAG, "gateway_addr.u_addr.ip4=%s", ip4addr_ntoa(&(gateway_addr.u_addr.ip4)));
		ping_config.target_addr = gateway_addr; // gateway IP address
	}

	ping_config.count = ESP_PING_COUNT_INFINITE; // ping in infinite mode, esp_ping_stop can stop it
	ping_config.interval_ms = interval_ms;
	ping_config.task_prio = task_prio;

	/* set callback functions */
	esp_ping_callbacks_t cbs = {};
		cbs.on_ping_success = cmd_ping_on_ping_success;
		cbs.on_ping_timeout = cmd_ping_on_ping_timeout;
		cbs.on_ping_end = cmd_ping_on_ping_end;
		cbs.cb_args = NULL;

	
	esp_ping_new_session(&ping_config, &cbs, &ping);
	//esp_ping_start(ping);
	ESP_LOGI(TAG, "ping start");
	return ESP_OK;
}

void ping_task(void *param)
{
	ping_cfg_t *ff = (ping_cfg_t *) param;

	ESP_LOGI(TAG, "Ping Device %s Ping Time %dsn.",ff->ip,ff->time);

	if (initialize_ping(1000, 2, ff->ip) == ESP_OK)
	{
		ESP_LOGI(TAG, "initialize_ping success");
		while(1)
		{
			if (*ff->cont==1) esp_ping_start(ping);
			if (ff->time<20) ff->time=20; 
			vTaskDelay(ff->time * 1000 / portTICK_PERIOD_MS);
		}
	}
	ESP_LOGE(TAG, "initialize_ping fail");
	vTaskDelete(NULL);
}

esp_err_t ping_system(char *ip, uint8_t ping_time)
{
	ping_cfg_t *aaa = (ping_cfg_t *)malloc(sizeof(ping_cfg_t));
	aaa->ip = ip;
	aaa->time = ping_time;
	continious = (uint8_t *)malloc(sizeof(uint8_t));
	*continious = 1;
	aaa->cont = continious;
	xTaskCreate(&ping_task, "ping_task", 4096, aaa, tskIDLE_PRIORITY, NULL);
	return ESP_OK;
}

void ping_stop(void)
{
	*continious = 0;
}
void ping_start(void)
{
	*continious = 1;
}


