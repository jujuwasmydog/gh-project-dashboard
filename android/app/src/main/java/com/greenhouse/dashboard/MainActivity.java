package com.greenhouse.dashboard;

import android.os.Bundle;
import android.webkit.WebResourceError;
import android.webkit.WebResourceRequest;
import android.webkit.WebSettings;
import android.webkit.WebView;
import android.webkit.WebViewClient;
import android.widget.TextView;

import androidx.appcompat.app.AppCompatActivity;
import androidx.swiperefreshlayout.widget.SwipeRefreshLayout;

public class MainActivity extends AppCompatActivity {

    // Update this to match your Raspberry Pi IP address
    private static final String DASHBOARD_URL = "http://192.168.1.100:1880/dashboard";

    private WebView webView;
    private SwipeRefreshLayout swipeRefresh;
    private TextView errorText;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);

        webView = findViewById(R.id.webview);
        swipeRefresh = findViewById(R.id.swipe_refresh);
        errorText = findViewById(R.id.error_text);

        setupWebView();

        swipeRefresh.setOnRefreshListener(() -> {
            errorText.setVisibility(android.view.View.GONE);
            webView.reload();
        });

        webView.loadUrl(DASHBOARD_URL);
    }

    private void setupWebView() {
        WebSettings settings = webView.getSettings();
        settings.setJavaScriptEnabled(true);
        settings.setDomStorageEnabled(true);
        settings.setLoadWithOverviewMode(true);
        settings.setUseWideViewPort(true);
        settings.setBuiltInZoomControls(true);
        settings.setDisplayZoomControls(false);

        webView.setWebViewClient(new WebViewClient() {
            @Override
            public void onPageFinished(WebView view, String url) {
                swipeRefresh.setRefreshing(false);
            }

            @Override
            public void onReceivedError(WebView view, WebResourceRequest request,
                                        WebResourceError error) {
                swipeRefresh.setRefreshing(false);
                if (request.isForMainFrame()) {
                    errorText.setVisibility(android.view.View.VISIBLE);
                    errorText.setText("Cannot reach dashboard.\nCheck IP: " + DASHBOARD_URL);
                }
            }
        });
    }

    @Override
    public void onBackPressed() {
        if (webView.canGoBack()) {
            webView.goBack();
        } else {
            super.onBackPressed();
        }
    }
}
